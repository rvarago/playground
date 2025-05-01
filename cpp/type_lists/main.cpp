#include <cstddef>
#include <iostream>
#include <string>
#include <utility>
#include <variant>
#include <vector>

struct ResourceData_A {
  int id{};
};

struct ResourceData_B {
  std::string name{};
};

struct ResourceData_C {};

namespace detail {

using ResourceData =
    std::variant<ResourceData_A, ResourceData_B, ResourceData_C>;

template <typename R, size_t I = 0> consteval auto resource_index() -> size_t {
  static_assert(I < std::variant_size_v<ResourceData>,
                "resource type not found");

  if constexpr (std::is_same_v<R,
                               std::variant_alternative_t<I, ResourceData>>) {
    // TODO: Guard against duplicated resource types.
    return I;
  } else {
    return resource_index<R, I + 1>();
  }
}

} // namespace detail

class ResourceFactory;

struct Resource {
  struct Key {
    size_t type_index{};
    size_t in_container_index{};
  };

  template <typename R> constexpr auto is() -> bool {
    return key.type_index == detail::resource_index<R>();
  }

  template <typename R> constexpr auto as() -> R &;

  int amount{};
  Key key{};
  ResourceFactory *factory{}; // TODO: Re-think ownership, maybe a weak_ptr?
};

class ResourceFactory {

public:
  template <typename R, typename... Args>
  constexpr auto make(int amount, Args &&...args) -> Resource {
    std::pair<std::vector<R> &, size_t> slot = findSlot<R>();
    slot.first.push_back(R{std::forward<Args>(args)...});
    auto const in_container_index = slot.first.size() - 1;

    return Resource{amount,
                    Resource::Key{
                        .type_index = slot.second,
                        .in_container_index = in_container_index,
                    },
                    this};
  }

private:
  template <typename R>
  constexpr auto findSlot() -> std::pair<std::vector<R> &, size_t> {
    // TODO: Enforce consistency between indices and containers, pull magic
    // numbers into constants, etc.
    constexpr auto type_index = detail::resource_index<R>();
    if constexpr (type_index == 0) {
      return {as, type_index};
    } else if constexpr (type_index == 1) {
      return {bs, type_index};
    } else if constexpr (type_index == 2) {
      return {cs, type_index};
    } else {
      throw "resource container not found";
    }
  }

  friend Resource;

  std::vector<ResourceData_A> as{};
  std::vector<ResourceData_B> bs{};
  std::vector<ResourceData_C> cs{};
};

// PRE: Resource holds an R, otherwise this is UB.
template <typename R> constexpr auto Resource::as() -> R & {
  std::pair<std::vector<R> &, size_t> slot = factory->findSlot<R>();
  return slot.first[key.in_container_index];
}

auto main(int, char *[]) -> int {
  ResourceFactory factory{};
  auto r1 = factory.make<ResourceData_A>(100, 42);
  std::cout << r1.is<ResourceData_A>() << std::endl;
  std::cout << r1.is<ResourceData_B>() << std::endl;
  std::cout << r1.is<ResourceData_C>() << std::endl;
  std::cout << r1.amount << std::endl;
  std::cout << r1.as<ResourceData_A>().id << std::endl;

  auto r2 = factory.make<ResourceData_B>(200, "foo");
  std::cout << r2.is<ResourceData_A>() << std::endl;
  std::cout << r2.is<ResourceData_B>() << std::endl;
  std::cout << r2.is<ResourceData_C>() << std::endl;
  std::cout << r2.amount << std::endl;
  std::cout << r2.as<ResourceData_B>().name << std::endl;

  auto r3 = factory.make<ResourceData_A>(300, 666);
  std::cout << r3.is<ResourceData_A>() << std::endl;
  std::cout << r3.is<ResourceData_B>() << std::endl;
  std::cout << r3.is<ResourceData_C>() << std::endl;
  std::cout << r3.amount << std::endl;
  std::cout << r3.as<ResourceData_A>().id << std::endl;

  return 0;
}
