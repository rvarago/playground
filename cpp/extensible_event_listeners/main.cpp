#include <algorithm>
#include <any>
#include <functional>
#include <iostream>
#include <optional>
#include <typeindex>
#include <unordered_map>
#include <vector>

class type_map {
public:
  template <typename Value> auto set(Value value) -> void {
    entries_.insert_or_assign(to_key<Value>(), std::move(value));
  }

  template <typename Value> auto contains() const -> bool {
    return entries_.contains(to_key<Value>());
  }

  template <typename Value>
  auto get_ref() -> std::optional<std::reference_wrapper<Value>> {
    if (!contains<Value>()) {
      return {};
    } else {
      return std::ref(std::any_cast<Value &>(entries_[to_key<Value>()]));
    }
  }

private:
  std::unordered_map<std::type_index, std::any> entries_{};

  template <typename Value> static auto to_key() {
    return std::type_index{typeid(Value)};
  }
};

template <typename Event> using ListenerPtr = void (*)(Event const &);
template <typename Event>
using ListenerStdFunction = std::function<void(Event const &)>;

template <template <typename Event> typename Listener = ListenerPtr,
          template <typename Event> typename Container = std::vector>
class event_dispatcher {
  template <typename Event>
  using listener_container = Container<Listener<Event>>;

public:
  template <typename Event> auto register_on(Listener<Event> listener) -> void {
    if (!event_to_listeners_.contains<listener_container<Event>>()) {
      event_to_listeners_.set<listener_container<Event>>({});
    }
    event_to_listeners_.get_ref<listener_container<Event>>()->get().push_back(
        std::move(listener));
  }

  template <typename Event> auto trigger(Event const &event) -> void {
    auto const listeners =
        event_to_listeners_.get_ref<listener_container<Event>>();
    if (listeners) {
      std::ranges::for_each(listeners->get(), [&event](auto const &listener) {
        listener(event);
      });
    }
  }

private:
  type_map event_to_listeners_{};
};

struct on_click {
  size_t mouse_x;
  size_t mouse_y;
};

int main() {
  auto dispatcher_with_listener_as_ptr = event_dispatcher{};

  dispatcher_with_listener_as_ptr.register_on<on_click>([](auto const &ev) {
    std::cout << "(x, y) = " << '(' << ev.mouse_x << ',' << ev.mouse_y << ')'
              << std::endl;
  });

  dispatcher_with_listener_as_ptr.trigger(on_click{600, 400});

  auto dispatcher_with_listener_as_stdfunction =
      event_dispatcher<ListenerStdFunction>{};

  dispatcher_with_listener_as_stdfunction.register_on<on_click>(
      [offset = 100](auto const &ev) {
        std::cout << "(x, y)[offset] = " << '(' << ev.mouse_x << ','
                  << ev.mouse_y << ")[" << offset << ']' << std::endl;
      });

  dispatcher_with_listener_as_stdfunction.trigger(on_click{600, 400});

  return 0;
}
