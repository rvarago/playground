#include <algorithm>
#include <cstddef>
#include <exception>
#include <functional>
#include <iostream>
#include <memory>
#include <ranges>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

// TODO: Explore
// * Listeners that can subscribe, e.g. by coping the the list to avoid
// iteration invalidation.
// * Multi-threading with a working thread that process events.
// * Open set of events, e.g. with std::any.
// * Listeners that can return to indicate whether to re-schedule it.
// * Different ownership models other than ref-counting.
// * Template Handler instead of hard-coding std::function.

struct MouseClicked {
  int x, y;
};

struct KeyPressed {
  int code;
};

// A closed set of events with no duplicated types.
using Event = std::variant<MouseClicked, KeyPressed>;

// pre: Event has no duplicates.
template <typename E, size_t I = 0> consteval auto event_index() -> size_t {
  if constexpr (I >= std::variant_size_v<Event>) {
    throw "E is not a valid Event";
  }
  if constexpr (std::is_same_v<E, std::variant_alternative_t<I, Event>>) {
    return I;
  } else {
    return event_index<E, I + 1>();
  }
}

class Dispatcher;

// Allow un-subscribing upon subscription.
class SubscriptionToken {
public:
  SubscriptionToken(SubscriptionToken const &) = delete;
  SubscriptionToken &operator=(SubscriptionToken const &) = delete;

  SubscriptionToken(SubscriptionToken &&) = default;
  SubscriptionToken &operator=(SubscriptionToken &&) = default;

  constexpr void detach() && { m_detached = true; }

  void unsubscribe() {
    if (!m_detached) {
      m_detached = true;
      if (auto dispatcher = m_dispatcher.lock(); dispatcher) {
        m_unsubscriber(*dispatcher, *this);
      }
    }
  }

  ~SubscriptionToken() noexcept { unsubscribe(); }

private:
  friend class Dispatcher;

  using DispatcherHandle = std::weak_ptr<Dispatcher>;
  using Unsubscriber = void (*)(Dispatcher &, SubscriptionToken const &);

  SubscriptionToken(DispatcherHandle dispatcher, Unsubscriber unsubscriber,
                    size_t event_id, size_t listener_id)
      : m_dispatcher(dispatcher),
        m_unsubscriber(unsubscriber),
        m_event_id{event_id},
        m_listener_id{listener_id} {}

  constexpr auto is_detached() const -> bool { return m_detached; }

  constexpr auto listener_id() const -> size_t { return m_listener_id; }

  constexpr auto event_id() const -> size_t { return m_event_id; }

  DispatcherHandle m_dispatcher;
  Unsubscriber m_unsubscriber;
  size_t m_event_id;
  size_t m_listener_id;
  bool m_detached{false};
};

class Dispatcher : public std::enable_shared_from_this<Dispatcher> {
  struct use_make_factory {
    explicit constexpr use_make_factory() = default;
  };

  struct IllegalSubscribeException : std::exception {
    const char *what() const noexcept override {
      return "can't subscribe inside a listener";
    }
  };

public:
  template <typename E> using Listener = std::function<void(E const &)>;

  static auto make() -> std::shared_ptr<Dispatcher> {
    return std::make_shared<Dispatcher>(use_make_factory{});
  }

  template <typename E>
    requires std::is_constructible_v<Event, E>
  auto subscribe(Listener<E> listener) -> SubscriptionToken {
    if (is_dispatching) {
      throw IllegalSubscribeException{};
    }

    constexpr auto event_id = event_index<E>();
    auto listener_id = ++m_next_listener_id;

    m_listeners[event_id].push_back(
        {listener_id, [listener = std::move(listener)](Event const &e) {
           listener(std::get<E>(e));
         }});

    return SubscriptionToken{
        shared_from_this(),
        [](Dispatcher &dispatcher, SubscriptionToken const &token) {
          dispatcher.unsubscribe(token);
        },
        event_id, listener_id};
  }

  void unsubscribe(SubscriptionToken const &token) {
    if (token.is_detached()) {
      auto &listeners = m_listeners[token.event_id()];
      std::erase_if(listeners, [&](auto const &e) {
        return e.first == token.listener_id();
      });
    }
  }

  template <typename E>
    requires std::is_constructible_v<Event, E>
  constexpr auto dispatch(E const &e) -> size_t {
    is_dispatching = true;
    auto const total_receivers = do_dispatch(e);
    is_dispatching = false;
    return total_receivers;
  }

  explicit Dispatcher(use_make_factory) {}

private:
  using event_id_t = size_t;
  using listener_id_t = size_t;

  template <typename E>
    requires std::is_constructible_v<Event, E>
  constexpr auto do_dispatch(E const &e) -> size_t {
    constexpr auto event_id = event_index<E>();
    if (auto it = m_listeners.find(event_id); it != m_listeners.cend()) {
      auto const &listeners = it->second;
      std::ranges::for_each(listeners,
                            [&](auto const &listener) { listener.second(e); });
      return listeners.size();
    } else {
      return 0;
    }
  }

  std::unordered_map<
      event_id_t,
      std::vector<std::pair<listener_id_t, std::function<void(Event const &)>>>>
      m_listeners{};
  listener_id_t m_next_listener_id{0};
  bool is_dispatching{false};
};

int main(int, char *[]) {
  auto dispatcher = Dispatcher::make();

  // won't be processed: we un-subscribe at the end of the statemement via dtor.
  dispatcher->subscribe<MouseClicked>([](MouseClicked const &e) {
    std::cout << "MouseClicked (token destroyed): " << e.x << ", " << e.y
              << '\n';
  });

  // won't compile: we can't construct an Event from an int.
  // dispatcher->subscribe<int>([](auto const x) {});

  // will be processed: we detached the token.
  dispatcher
      ->subscribe<MouseClicked>([&](MouseClicked const &e) {
        // will throw: listeners can't subscribe, lest we'd invalid iterators.
        // dispatcher->subscribe<MouseClicked>([](MouseClicked const &) {});
        std::cout << "MouseClicked (detached): " << e.x << ", " << e.y << '\n';
      })
      .detach();

  // will be processed: we keep the token alive by the time we trigger.
  auto _extended =
      dispatcher->subscribe<MouseClicked>([](MouseClicked const &e) {
        std::cout << "MouseClicked (extended): " << e.x << ", " << e.y << '\n';
      });

  std::cout << "processed by: "
            << dispatcher->dispatch(MouseClicked{.x = 300, .y = 100}) << '\n';

  return 0;
}
