#include <condition_variable>
#include <cstddef>
#include <cstdio>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <stop_token>
#include <syncstream>
#include <thread>
#include <utility>
#include <vector>

using namespace std::chrono_literals;

template <typename T> class unbounded_sync_queue {
public:
  void enqueue(T arg) {
    {
      std::lock_guard _lock{mtx_};
      entries_.push(std::move(arg));
    }
    nonempty_cond.notify_all();
  }

  std::optional<T> take(std::stop_token stop_token) {
    std::unique_lock lock{mtx_};
    if (nonempty_cond.wait(lock, stop_token,
                           [this] { return !entries_.empty(); })) {
      auto const front = std::move(entries_.front());
      entries_.pop();
      return {front};
    } else {
      return std::nullopt;
    }
  }

private:
  std::queue<T> entries_;
  std::mutex mtx_;
  std::condition_variable_any nonempty_cond;
};

class thread_pool {
public:
  using work = void (*)();

  explicit thread_pool(size_t size)
      : pending_work_(std::make_shared<unbounded_sync_queue<work>>()) {

    workers_.reserve(size);

    for (auto i = 0; i < size; ++i) {
      workers_.push_back(std::jthread{drain, std::ref(pending_work_)});
    }
  }

  void submit(work w) { pending_work_->enqueue(w); }

private:
  static void drain(std::stop_token stop_token,
                    std::shared_ptr<unbounded_sync_queue<work>> pending_work) {
    while (!stop_token.stop_requested()) {
      auto const work = pending_work->take(stop_token);
      if (work) {
        (*work)();
      }
    }
  }
  std::vector<std::jthread> workers_{};
  std::shared_ptr<unbounded_sync_queue<work>> pending_work_{};
};

int main(int argc, char *argv[]) {
  thread_pool pool{3};

  pool.submit([] {
    std::osyncstream{std::cout}
        << "work 1 (thread: " << std::this_thread::get_id() << ")\n";
    std::this_thread::sleep_for(2s);
  });
  pool.submit([] {
    std::osyncstream{std::cout}
        << "work 2 (thread: " << std::this_thread::get_id() << ")\n";
    std::this_thread::sleep_for(2s);
  });
  pool.submit([] {
    std::osyncstream{std::cout}
        << "work 3 (thread: " << std::this_thread::get_id() << ")\n";
    std::this_thread::sleep_for(2s);
  });
  pool.submit([] {
    std::osyncstream{std::cout}
        << "work 4 (thread: " << std::this_thread::get_id() << ")\n";
    std::this_thread::sleep_for(2s);
  });

  std::this_thread::sleep_for(10s);

  return 0;
}
