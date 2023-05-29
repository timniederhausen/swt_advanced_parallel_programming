// Pull in the reference implementation of P2300:
#include <stdexec/execution.hpp>

#include <string>
#include <iostream>

#include "exec/static_thread_pool.hpp"

// Make everything look as standard as possible
namespace ex = stdexec;
namespace this_thread { // can't extend std::this_thread without violating the std
using stdexec::sync_wait;
}

// namespace ex = std::execution;

namespace example1 {

// Imagine we already have an expensive |grade_report()| function
std::string grade_report(const std::string& report)
{
  if (report.empty())
    throw std::runtime_error("Bad report");
  if (report.find("Tasks in C++") != std::string::npos)
    return "1.0";
  return std::string{}; // can't grade this report!
}

// Could also just be `ex::sender`
ex::sender_of<ex::set_value_t(std::string)>
auto async_grade_report(std::string report)
{
  // Simplest possible version, just forward arguments to the synchronous function
  return ex::just(std::move(report)) | ex::then(grade_report);
}

ex::sender auto async_evaluate_grade(std::string grade)
{
  return ex::just(std::move(grade)) | ex::then([] (const std::string& grade) {
    std::thread::id this_id = std::this_thread::get_id();
    std::cout << "thread " << this_id << " grading...\n";

    std::cout << "Grade: " << grade << '\n';
    return grade != "5.0"; // return if we're passing!
  });
}

}

void run_example()
{
  using namespace example1;

  ex::sender auto sender = ex::just("Tasks in C++") |
                           ex::let_value(async_grade_report) |
                           ex::let_value(async_evaluate_grade);
  // Since we didn't specify a scheduler, this call runs our task graph
  // on the same thread, returning the result directly.
  auto [passed] = this_thread::sync_wait(std::move(sender)).value();
  std::cout << "R1 Passed?: " << passed << std::endl;
}

// Example using not-proposed thread pool APIs:
void run_example_in_thread_pool()
{
  using namespace example1;

  exec::static_thread_pool ctx{8};
  ex::scheduler auto sch = ctx.get_scheduler();

  ex::sender auto sender = ex::transfer_just(sch, "Tasks not in C++") |
                           ex::let_value(async_grade_report) |
                           ex::let_value(async_evaluate_grade);
  auto [passed] = this_thread::sync_wait(std::move(sender)).value();
  std::cout << "R2 Passed?: " << passed << std::endl;
}

int main(int argc, const char* argv[])
{
  std::thread::id this_id = std::this_thread::get_id();
  std::cout << "main thread " << this_id << '\n';

  run_example();
  run_example_in_thread_pool();
}
