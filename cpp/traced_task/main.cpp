#include <coroutine>
#include <cxxabi.h>
#include <dlfcn.h>
#include <exception>
#include <execinfo.h>
#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace util {
template <typename T>
using unique_ptr_for_free = std::unique_ptr<T, auto(*)(void *)->void>;

template <typename T>
constexpr auto make_unique_for_free(T *ptr) -> unique_ptr_for_free<T> {
  return unique_ptr_for_free<T>{ptr, std::free};
}

constexpr auto ptr_to_string(const char *ptr) -> std::string {
  return ptr ? std::string{ptr} : std::string{};
}
} // namespace util

// TODO: Replace this with C++23 std::backtrace.
namespace my_backtrace {

struct StackFrame {
  struct SymbolicInfo {
    std::string symbol;
    std::string fileName;
    uintptr_t offset = 0;
  };

  void *address;
  SymbolicInfo symbolicInfo;
};

using StackTrace = std::vector<StackFrame>;

auto intoSymbolicInfo(void *address, const char *symbol_backtrace)
    -> StackFrame::SymbolicInfo {

  Dl_info dl_info;
  if (!dladdr(address, &dl_info)) {
    return {};
  }

  auto const symbol = [&] {
    if (!dl_info.dli_sname) {
      return util::ptr_to_string(symbol_backtrace);
    }

    auto status = -1;
    auto const symbol_demangled = util::make_unique_for_free(
        abi::__cxa_demangle(dl_info.dli_sname, nullptr, nullptr, &status));

    return status != 0 ? util::ptr_to_string(symbol_demangled.get())
                       : util::ptr_to_string(symbol_backtrace);
  }();

  auto const fileName = util::ptr_to_string(dl_info.dli_fname);

  auto const offset = [&] {
    if (dl_info.dli_fbase) {
      return static_cast<uintptr_t>(
          static_cast<std::byte *>(address) -
          static_cast<std::byte *>(dl_info.dli_fbase));
    } else {
      return uintptr_t{0};
    }
  }();

  return StackFrame::SymbolicInfo{
      .symbol = symbol, .fileName = fileName, .offset = offset};
}

template <int DEPTH_MAX = 128> auto current() -> StackTrace {
  auto stackTrace = StackTrace{};

  void *callstack[DEPTH_MAX];
  auto const nframes = backtrace(callstack, DEPTH_MAX);

  if (nframes <= 0) {
    return stackTrace;
  }

  auto const callstack_symbols =
      util::make_unique_for_free(backtrace_symbols(callstack, nframes));

  if (!callstack_symbols) {
    return stackTrace;
  }

  // Skip first 2 frames (backtrace and this function).
  // TODO: Double-check this.
  for (int i = 2; i < nframes; ++i) {
    auto const address = callstack[i];

    auto const symbolicInfo =
        intoSymbolicInfo(address, callstack_symbols.get()[i]);

    auto const frame =
        StackFrame{.address = address, .symbolicInfo = symbolicInfo};

    stackTrace.push_back(frame);
  }
  return stackTrace;
}

auto toString(const StackTrace &stackTrace) -> std::string {
  std::stringstream ss{};

  for (size_t i = 0; i < stackTrace.size(); ++i) {
    const auto &frame = stackTrace[i];

    ss << "#" << i << ": " << frame.address << " ";

    if (!frame.symbolicInfo.symbol.empty()) {
      ss << frame.symbolicInfo.symbol;
    } else {
      ss << "???";
    }

    if (frame.symbolicInfo.offset > 0) {
      ss << " +" << frame.symbolicInfo.offset;
    }

    if (!frame.symbolicInfo.fileName.empty()) {
      ss << " in " << frame.symbolicInfo.fileName;
    }

    ss << '\n';
  }
  return ss.str();
}

} // namespace my_backtrace

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

void reportExceptionToFirebase(const std::exception_ptr exceptionPtr,
                               const my_backtrace::StackTrace &stackTrace) {
  try {
    if (exceptionPtr) {
      std::rethrow_exception(exceptionPtr);
    }
  } catch (const std::exception &e) {
    auto const stackTraceStr = my_backtrace::toString(stackTrace);
    firebase::crashlytics::RecordException(typeid(e).name(), e.what(),
                                           stackTraceStr.c_str());
  } catch (...) {
    auto const stackTraceStr = my_backtrace::toString(stackTrace);
    firebase::crashlytics::RecordException(
        "Unknown Exception", "Unhandled exception of unknown type",
        stackTraceStr.c_str());
  }
}

// TODO: Add support for T = void.
// TODO: Write an owning_handle that is non-copyable, moves properly and knows
// how to destroy itself and therefore we don't need write special member
// functions for TracedTask.
template <typename T> class TracedTask {
public:
  struct promise_type;
  using handle_type = std::coroutine_handle<promise_type>;

  struct promise_type {
    struct Success {
      T value;
    };
    std::variant<std::exception_ptr, Success> result;
    std::vector<my_backtrace::StackFrame> stackTrace;

    TracedTask get_return_object() {
      return TracedTask{handle_type::from_promise(*this)};
    }

    std::suspend_never initial_suspend() {
      // Capture stack trace at the beginning.
      stackTrace = my_backtrace::current();
      return {};
    }

    auto final_suspend() noexcept {
      struct final_awaiter {
        bool await_ready() noexcept { return false; }

        std::coroutine_handle<> await_suspend(handle_type h) noexcept {
          // Report any unhandled exception to Firebase before terminating.
          auto &p = h.promise();
          if (auto *e = std::get_if<std::exception_ptr>(&p.result); e) {
            reportExceptionToFirebase(*e, p.stackTrace);
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
      result = Success{std::forward<U>(value)};
    }

    // void return_void()
    //   requires std::is_same_v<T, void>
    // {}

    void unhandled_exception() { result = std::current_exception(); }
  };

  TracedTask() : handle(nullptr) {}

  TracedTask(handle_type h) : handle(h) {}

  ~TracedTask() {
    if (handle) {
      handle.destroy();
    }
  }

  TracedTask(const TracedTask &) = delete;
  TracedTask &operator=(const TracedTask &) = delete;

  TracedTask(TracedTask &&other) noexcept : handle(other.handle) {
    other.handle = nullptr;
  }
  TracedTask &operator=(TracedTask &&other) noexcept {
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
      throw std::runtime_error("TracedTask not completed");
    }

    struct accessor {
      auto operator()(typename promise_type::Success s) -> T { return s.value; }
      auto operator()(std::exception_ptr e) -> T { std::rethrow_exception(e); }
    };

    return std::visit(accessor{}, handle.promise().result);
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
void runTask(TracedTask<T> &&task, std::string_view taskName) {
  std::cout << "Running task: " << taskName << std::endl;
  try {
    task.run();
    std::cout << "Task completed with result: " << task.result() << std::endl;
  } catch (const std::exception &e) {
    std::cout << "Caught exception: " << e.what() << std::endl;
  }
}

// FIXME: It doesn't capture traces from regular routines.
// auto foo() -> int { throw std::runtime_error{"boom from foo"}; }

// This one throws an exception.
auto thirdLevelTask() -> TracedTask<int> {
  // foo();
  std::cout << "  Third level task - Throwing exception" << std::endl;
  // This simulates some deep operation failing.
  throw std::runtime_error("Exception from third level task");
  co_return 42; // Never reached
}

auto secondLevelTask() -> TracedTask<int> {
  std::cout << " Second level task - Calling third level" << std::endl;
  // This await will propagate the exception.
  int result = co_await thirdLevelTask();
  co_return result * 2; // Never reached.
}

// Top-level coroutine - calls the second level and throws.
auto topLevelTask() -> TracedTask<int> {
  std::cout << "Top level task - Calling second level" << std::endl;

  // We don't handle the exception here, so it will be reported.
  int result = co_await secondLevelTask();
  std::cout << "Result: " << result << std::endl; // Never reached

  co_return 1;
}

// Another top-level task that properly handles exceptions.
TracedTask<int> handledExceptionTask() {
  std::cout << "Task with handled exception - Calling second level"
            << std::endl;
  try {
    int result = co_await secondLevelTask();
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
  runTask(topLevelTask(), "TopLevelTask");
  std::cout << "\n";

  // Example 2: Handled exception across coroutines.
  // std::cout << "Example 2: Handled exception across coroutines\n";
  // runTask(handledExceptionTask(), "HandledExceptionTask");

  return 0;
}
