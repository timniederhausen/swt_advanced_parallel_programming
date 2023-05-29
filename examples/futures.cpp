#include <future>
#include <string>
#include <stdexcept>
#include <iostream>

namespace example1 {

std::future<std::string> async_grade_report(std::string report)
{
  std::promise<std::string> grade_promise;
  std::future<std::string> grade_future = grade_promise.get_future();

  // Move our promise into the lambda that we run on a new thread.
  // Note that this requires us to call get_future() before this point!
  std::thread([grade_promise = std::move(grade_promise),
               report = std::move(report)] () mutable {
    if (report.empty()) {
      std::runtime_error err("Bad report");
      grade_promise.set_exception(std::make_exception_ptr(err));
    } else if (report.find("Tasks in C++") != std::string::npos) {
      grade_promise.set_value("1.0");
    }
    // If we don't call set_value() or set_exception(), we'll
    // end up with std::future_error (broken_promise)
  }).detach();

  return grade_future;
}

}

namespace example2 {

// Imagine we already have an expensive |grade_report()| function
std::string grade_report(const std::string& report)
{
  if (report.empty())
    throw std::runtime_error("Bad report");
  if (report.find("Tasks in C++") != std::string::npos)
    return "1.0";
  return std::string{}; // can't grade this report!
}

// std::packaged_task allows us to turn it into an awaitable future
std::future<std::string> async_grade_report(std::string report)
{
  std::packaged_task task{grade_report};
  std::future<std::string> grade_future = task.get_future();
  // Launch the computation in a new thread (like in the last version)
  std::thread(std::move(task), std::move(report)).detach();
  return grade_future;
}

}

namespace example3 {

using example2::grade_report; // no copy&paste!

std::future<std::string> async_grade_report(std::string report)
{
  // Since we just always launch a new thread here, std::async()
  // allows us to shorten our code!
  return std::async(std::launch::async, grade_report, std::move(report));
}

}

int main(int argc, const char* argv[])
{
}
