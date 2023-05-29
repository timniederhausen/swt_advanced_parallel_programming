// #include order matters here because stdexec uses reserved names :((

// Pull in the reference implementation of P2300:
#include <stdexec/execution.hpp>
// Keep track of spawned work in an async_scope:
#include <exec/async_scope.hpp>
// Use a thread pool
#include <exec/static_thread_pool.hpp>

#include "deformer.hpp"
#include "deformer_io.hpp"

#include <benchmark/benchmark.h>

#include <future>
#include <span>
#include <iostream>

namespace ex = stdexec;

//
// Helpers for benchmarking the deformation process
//

// heh, dev PC runs out of memory :(
// final run on server is fine
// set to false to also measure more realistic memory usage
inline constexpr bool IsDeformTimingOnlyMode = true;

static const std::vector<deformanble_mesh>& get_problem_for_strong_scaling()
{
  static const auto meshes = load_meshes("meshes.bin");
  return meshes;
}

// NOTE: only call this from the main thread!
static const std::vector<deformanble_mesh>& get_problem_for_weak_scaling(uint32_t expansion)
{
  const auto& meshes = get_problem_for_strong_scaling();
  if (expansion == 1)
    return meshes;

  static std::vector<deformanble_mesh> expanded_problem;
  expanded_problem.clear();
  expanded_problem.reserve(meshes.size() * expansion);
  for (uint32_t i = 0; i != expansion; ++i)
    expanded_problem.insert(expanded_problem.end(), meshes.begin(), meshes.end());
  return expanded_problem;
}

//
// Parallel deformers using the various methods
//

// Needed for the future-based versions where we are responsible for splitting work!
std::vector<pwnt3432_vertex> run_benchmark(std::span<const deformanble_mesh> meshes)
{
  // In the real-world we'd use another output vertex type, here it doesn't matter
  std::vector<pwnt3432_vertex> output;
  {
    std::size_t num_vertices = 0;
    for (const auto& mesh : meshes) {
      if constexpr (IsDeformTimingOnlyMode)
        num_vertices = std::max(num_vertices, mesh.vertices.size());
      else
        num_vertices += mesh.vertices.size();
    }
    output.resize(num_vertices);
  }

  std::size_t output_offset = 0;
  for (const auto& mesh : meshes) {
    deform_vertices(mesh.vertices.data(), output.data() + output_offset,
                    mesh.bone_transforms.data(), mesh.vertices.size());
    if constexpr (!IsDeformTimingOnlyMode)
      output_offset += mesh.vertices.size();
  }
  if constexpr (IsDeformTimingOnlyMode)
    return {};
  return output;
}

template <typename FutureAdapter>
std::vector<pwnt3432_vertex> run_with_futures(std::span<const deformanble_mesh> meshes,
                                              uint32_t threads, FutureAdapter&& adapter)
{
  std::vector<
    std::future<std::vector<pwnt3432_vertex>>
  > futures;
  /*if constexpr (IsStrongScaling) {*/
    const auto per_thread_size = meshes.size() / threads;
    std::size_t offset = 0;
    for (uint32_t i = 0; i != threads; ++i) {
      if (i + 1 == threads)
        futures.emplace_back(adapter(meshes.subspan(offset)));
      else
        futures.emplace_back(adapter(meshes.subspan(offset, per_thread_size)));
      offset += per_thread_size;
    }
  /*} else {
    // Problem grows with number of processors (\approx number of threads)
    // Just run the whole thing on all threads?
    for (uint32_t i = 0; i != threads; ++i) {
      futures.emplace_back(std::async(run_benchmark, meshes));
    }
  }*/
  std::vector<pwnt3432_vertex> vertices;
  for (auto& f : futures) {
    const auto& sub_vertices = f.get();
    vertices.insert(vertices.end(), sub_vertices.begin(), sub_vertices.end());
  }
  return vertices;
}

std::future<std::vector<pwnt3432_vertex>> async_run_benchmark_manual(std::span<const deformanble_mesh> meshes)
{
  std::promise<std::vector<pwnt3432_vertex>> promise;
  auto future = promise.get_future();

  // Move our promise into the lambda that we run on a new thread.
  // Note that this requires us to call get_future() before this point!
  std::thread([promise = std::move(promise), meshes] () mutable {
    try {
      promise.set_value(run_benchmark(meshes));
    } catch (...) {
      try {
        promise.set_exception(std::current_exception());
      } catch(...) {} // set_exception() may throw too
    }
  }).detach();
  return future;
}

std::future<std::vector<pwnt3432_vertex>> async_run_benchmark_task(std::span<const deformanble_mesh> meshes)
{
  std::packaged_task<std::vector<pwnt3432_vertex>(std::span<const deformanble_mesh>)> task{run_benchmark};
  auto future = task.get_future();
  // Launch the computation in a new thread (like in the last version)
  std::thread(std::move(task), meshes).detach();
  return future;
}

std::vector<pwnt3432_vertex> reduce_vertices(std::span<const std::vector<pwnt3432_vertex>> results)
{
  std::vector<pwnt3432_vertex> vertices;
  if constexpr (!IsDeformTimingOnlyMode) {
   std::size_t num_vertices = 0;
   for (const auto& sub_vertices : results)
     num_vertices += sub_vertices.size();

    vertices.reserve(num_vertices);
    for (const auto& sub_vertices : results)
      vertices.insert(vertices.end(), sub_vertices.begin(), sub_vertices.end());
  }
  return vertices;
}

// bulk() over all meshes
ex::sender auto async_run_benchmark_stdexec(std::span<const deformanble_mesh> meshes)
{
  auto runner = [=](size_t i, std::vector<std::vector<pwnt3432_vertex>>& output_meshes) {
    const auto& mesh = meshes[i];
    output_meshes[i].resize(mesh.vertices.size());
    deform_vertices(mesh.vertices.data(), output_meshes[i].data(),
                    mesh.bone_transforms.data(), mesh.vertices.size());
    if constexpr (IsDeformTimingOnlyMode)
      output_meshes = {}; // need to deallocate here :(
  };

  return ex::just(std::vector<std::vector<pwnt3432_vertex>>{meshes.size()})
    | ex::bulk(meshes.size(), std::move(runner)) // can run in parallel
    | ex::then(reduce_vertices);
}

// bulk() over N threads
ex::sender auto async_run_benchmark_stdexec_bulk(ex::scheduler auto sched,
                                                 std::span<const deformanble_mesh> meshes,
                                                 std::size_t threads)
{
  const auto per_thread_size = meshes.size() / threads;
  auto runner = [=](std::size_t i, std::vector<std::vector<pwnt3432_vertex>>& output_meshes) {
    output_meshes[i] = run_benchmark(meshes.subspan(i * per_thread_size, per_thread_size));
  };

  return ex::transfer_just(sched, std::vector<std::vector<pwnt3432_vertex>>{threads})
    | ex::bulk(threads, std::move(runner)) // can run in parallel
    | ex::then(reduce_vertices);
}

//
// Benchmark harnesses
//

static void BM_Deform_ST(benchmark::State& state)
{
  const auto& meshes = get_problem_for_strong_scaling();
  for (auto _ : state)
    run_benchmark(meshes);
}
BENCHMARK(BM_Deform_ST);

enum class Scaling
{
  // i.e. fixed work per thread
  Weak,
  // i.e. fixed work over all threads
  Strong
};

template <Scaling S>
inline std::span<const deformanble_mesh> get_meshes(uint32_t threads = 1)
{
  switch (S) {
    case Scaling::Strong: return get_problem_for_strong_scaling();
    case Scaling::Weak: return get_problem_for_weak_scaling(threads);
  }
  // unreachable
}

template <Scaling S>
static void BM_Deform_MT_StdPromise(benchmark::State& state)
{
  const auto& meshes = get_meshes<S>(state.range(0));
  for (auto _ : state) {
    run_with_futures(meshes, state.range(0), [] (std::span<const deformanble_mesh> meshes) {
      return async_run_benchmark_manual(meshes);
    });
  }
}

template <Scaling S>
static void BM_Deform_MT_StdPkgTask(benchmark::State& state)
{
  const auto& meshes = get_meshes<S>(state.range(0));
  for (auto _ : state) {
    run_with_futures(meshes, state.range(0), [] (std::span<const deformanble_mesh> meshes) {
      return async_run_benchmark_task(meshes);
    });
  }
}

template <Scaling S>
static void BM_Deform_MT_StdAsync(benchmark::State& state)
{
  const auto& meshes = get_meshes<S>(state.range(0));
  for (auto _ : state) {
    run_with_futures(meshes, state.range(0), [] (std::span<const deformanble_mesh> meshes) {
      return std::async(run_benchmark, meshes);
    });
  }
}

template <Scaling S>
static void BM_Deform_MT_StdExec(benchmark::State& state)
{
  const auto& meshes = get_meshes<S>(state.range(0));

  // Create a thread pool and get a scheduler from it
  exec::static_thread_pool pool(state.range(0));
  ex::scheduler auto sched = pool.get_scheduler();

  for (auto _ : state) {
    // For fairness, use the _bulk version!
    stdexec::sync_wait(ex::on(sched, async_run_benchmark_stdexec_bulk(sched, meshes, state.range(0))));
  }

  pool.request_stop();
}

// XXX macro hell
#define SETUP_MT_BENCHMARK \
  ->Arg(2)->Arg(4)->Arg(8)->Arg(16)->Arg(32)->MeasureProcessCPUTime()->UseRealTime();

BENCHMARK(BM_Deform_MT_StdPromise<Scaling::Strong>) SETUP_MT_BENCHMARK;
BENCHMARK(BM_Deform_MT_StdPkgTask<Scaling::Strong>) SETUP_MT_BENCHMARK;
BENCHMARK(BM_Deform_MT_StdAsync<Scaling::Strong>) SETUP_MT_BENCHMARK;
BENCHMARK(BM_Deform_MT_StdExec<Scaling::Strong>) SETUP_MT_BENCHMARK;
BENCHMARK(BM_Deform_MT_StdPromise<Scaling::Weak>) SETUP_MT_BENCHMARK;
BENCHMARK(BM_Deform_MT_StdPkgTask<Scaling::Weak>) SETUP_MT_BENCHMARK;
BENCHMARK(BM_Deform_MT_StdAsync<Scaling::Weak>) SETUP_MT_BENCHMARK;
BENCHMARK(BM_Deform_MT_StdExec<Scaling::Weak>) SETUP_MT_BENCHMARK;

BENCHMARK_MAIN();
