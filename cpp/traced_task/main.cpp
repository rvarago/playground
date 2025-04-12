#include <coroutine>
#include <cxxabi.h>
#include <dlfcn.h>
#include <exception>
#include <execinfo.h>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace my_backtrace {
struct StackFrame {
  void *address;
  std::string symbol;
  std::string fileName;
  uintptr_t offset = 0;
  int lineNumber = 0;
};

std::vector<StackFrame> CaptureStackTrace() {
  std::vector<StackFrame> stackTrace;

  const int kMaxStackDepth = 128;
  void *callstack[kMaxStackDepth];
  int frames = backtrace(callstack, kMaxStackDepth);

  if (frames <= 0) {
    return stackTrace;
  }

  char **symbols = backtrace_symbols(callstack, frames);
  if (!symbols) {
    return stackTrace;
  }

  // Skip first frames which are related to this backtrace and this function.
  for (int i = 2; i < frames; i++) {
    StackFrame frame;
    frame.address = callstack[i];

    Dl_info info;
    if (dladdr(callstack[i], &info)) {
      if (info.dli_sname) {
        int status;
        char *demangled =
            abi::__cxa_demangle(info.dli_sname, nullptr, nullptr, &status);
        if (demangled) {
          frame.symbol = demangled;
          free(demangled);
        } else {
          frame.symbol = info.dli_sname;
        }
      } else {
        frame.symbol = symbols[i];
      }

      if (info.dli_fname) {
        frame.fileName = info.dli_fname;
      }

      if (info.dli_fbase) {
        frame.offset = (uintptr_t)callstack[i] - (uintptr_t)info.dli_fbase;
      }
    } else {
      frame.symbol = symbols[i];
    }

    stackTrace.push_back(frame);
  }

  free(symbols);
  return stackTrace;
}

std::string FormatStackTrace(const std::vector<StackFrame> &stackTrace) {
  std::stringstream ss;
  for (size_t i = 0; i < stackTrace.size(); i++) {
    const auto &frame = stackTrace[i];

    ss << "#" << i << ": " << frame.address << " ";

    if (!frame.symbol.empty()) {
      ss << frame.symbol;
    } else {
      ss << "???";
    }

    if (frame.offset > 0) {
      ss << " +" << frame.offset;
    }

    if (!frame.fileName.empty()) {
      ss << " in " << frame.fileName;
    }

    ss << "\n";
  }
  return ss.str();
}

} // namespace my_backtrace

// Fake Firebase.
namespace firebase {
namespace crashlytics {
void RecordException(const char *name, const char *reason,
                     const char *stack_trace) {
  std::cout << "\n===== FIREBASE CRASHLYTICS REPORT =====\n";
  std::cout << "Exception Type: " << name << "\n";
  std::cout << "Reason: " << reason << "\n";
  std::cout << "Stack Trace:\n" << stack_trace;
  std::cout << "======================================\n";
}
} // namespace crashlytics
} // namespace firebase

void ReportExceptionToFirebase(
    const std::exception_ptr &exception,
    const std::vector<my_backtrace::StackFrame> &stackTrace) {
  try {
    if (exception) {
      std::rethrow_exception(exception);
    }
  } catch (const std::exception &e) {
    std::string stackTraceStr = FormatStackTrace(stackTrace);
    firebase::crashlytics::RecordException(typeid(e).name(), e.what(),
                                           stackTraceStr.c_str());
  } catch (...) {
    std::string stackTraceStr = FormatStackTrace(stackTrace);
    firebase::crashlytics::RecordException(
        "Unknown Exception", "Unhandled exception of unknown type",
        stackTraceStr.c_str());
  }
}

// Task implementation with automatic exception reporting
template <typename T> class Task {
public:
  struct promise_type;
  using handle_type = std::coroutine_handle<promise_type>;

  struct promise_type {
    std::optional<T> result;
    std::exception_ptr exception = nullptr;
    std::vector<my_backtrace::StackFrame> stackTrace;

    Task get_return_object() { return Task{handle_type::from_promise(*this)}; }

    std::suspend_never initial_suspend() {
      // Capture stack trace at the beginning
      stackTrace = my_backtrace::CaptureStackTrace();
      return {};
    }

    auto final_suspend() noexcept {
      struct final_awaiter {
        bool await_ready() noexcept { return false; }

        std::coroutine_handle<> await_suspend(handle_type h) noexcept {
          // Report any unhandled exception to Firebase before terminating.
          auto &p = h.promise();
          if (p.exception) {
            ReportExceptionToFirebase(p.exception, p.stackTrace);
          }

          // We're done, allow the coroutine to be destroyed.
          return std::noop_coroutine();
        }

        void await_resume() noexcept {}
      };

      return final_awaiter{};
    }

    template <typename U = T>
    void return_value(U &&value)
    // requires(!std::is_same_v<T, void>)
    {
      result = std::forward<U>(value);
    }

    // void return_void()
    //   requires std::is_same_v<T, void>
    // {}

    void unhandled_exception() {
      // Store the exception and stack trace
      exception = std::current_exception();
    }
  };

  Task() : handle(nullptr) {}

  Task(handle_type h) : handle(h) {}

  ~Task() {
    if (handle) {
      handle.destroy();
    }
  }

  // Non-copyable
  Task(const Task &) = delete;
  Task &operator=(const Task &) = delete;

  // Movable
  Task(Task &&other) noexcept : handle(other.handle) { other.handle = nullptr; }

  Task &operator=(Task &&other) noexcept {
    if (this != &other) {
      if (handle) {
        handle.destroy();
      }
      handle = other.handle;
      other.handle = nullptr;
    }
    return *this;
  }

  bool is_ready() const { return handle && handle.done(); }

  T result() const {
    if (!is_ready()) {
      throw std::runtime_error("Task not completed");
    }

    if (handle.promise().exception) {
      std::rethrow_exception(handle.promise().exception);
    }

    if constexpr (!std::is_same_v<T, void>) {
      return *handle.promise().result;
    }
  }

  void run() {
    if (handle && !handle.done()) {
      handle.resume();
    }
  }

  bool await_ready() const noexcept { return is_ready(); }

  std::coroutine_handle<>
  await_suspend(std::coroutine_handle<> continuation) const noexcept {
    handle.resume();
    return continuation;
  }

  T await_resume() const { return result(); }

private:
  handle_type handle;
};

// Helper function to run a task and print any caught exceptions
template <typename T>
void RunTask(Task<T> &&task, const std::string &taskName) {
  std::cout << "Running task: " << taskName << std::endl;
  try {
    task.run();
    if constexpr (!std::is_same_v<T, void>) {
      if (task.is_ready()) {
        std::cout << "Task completed with result: " << task.result()
                  << std::endl;
      }
    } else {
      if (task.is_ready()) {
        std::cout << "Task completed successfully." << std::endl;
      }
    }
  } catch (const std::exception &e) {
    std::cout << "Caught exception: " << e.what() << std::endl;
  }
}

// Third-level coroutine - this one throws an exception.
Task<int> ThirdLevelTask() {
  std::cout << "  Third level task - Throwing exception" << std::endl;
  // This simulates some deep operation failing.
  throw std::runtime_error("Exception from third level task");
  co_return 42; // Never reached
}

// Second-level coroutine - calls the third level.
Task<int> SecondLevelTask() {
  std::cout << " Second level task - Calling third level" << std::endl;
  // This await will propagate the exception.
  int result = co_await ThirdLevelTask();
  co_return result * 2; // Never reached.
}

// Top-level coroutine - calls the second level.
Task<int> TopLevelTask() {
  std::cout << "Top level task - Calling second level" << std::endl;
  try {
    // We don't handle the exception here, so it will be reported.
    int result = co_await SecondLevelTask();
    std::cout << "Result: " << result << std::endl; // Never reached
  } catch (...) {
    // We could handle it here, but we don't to demonstrate reporting
    // If we uncomment this, the exception would be handled and not reported
    // std::cout << "Exception caught and handled in top level" << std::endl;
    throw; // Re-throw to demonstrate propagation.
  }
  co_return 1;
}

// Another top-level task that properly handles exceptions.
Task<int> HandledExceptionTask() {
  std::cout << "Task with handled exception - Calling second level"
            << std::endl;
  try {
    int result = co_await SecondLevelTask();
    std::cout << "Result: " << result << std::endl; // Never reached
  } catch (const std::exception &e) {
    // We handle the exception here, so it won't be reported to Firebase.
    std::cout << "Exception caught and handled: " << e.what() << std::endl;
  }
  co_return 1;
}

int main() {
  std::cout << "=== Nested Coroutines with Exception Tracing ===\n\n";

  // Example 1: Unhandled exception propagating through multiple coroutines.
  std::cout << "Example 1: Unhandled exception through nested coroutines\n";
  RunTask(TopLevelTask(), "TopLevelTask");
  std::cout << "\n";

  // Example 2: Handled exception across coroutines.
  std::cout << "Example 2: Handled exception across coroutines\n";
  RunTask(HandledExceptionTask(), "HandledExceptionTask");

  return 0;
}
