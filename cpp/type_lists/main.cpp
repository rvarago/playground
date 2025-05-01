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

class ResourceFactory;

class Resource {
public:
  template <typename R> [[nodiscard]] constexpr auto is() -> bool;

  template <typename R> [[nodiscard]] constexpr auto as() -> R &;

  [[nodiscard]] auto amount() -> int { return amount_; }

private:
  friend ResourceFactory;

  struct Key {
    size_t type_index{};
    size_t in_container_index{};
  };

  explicit Resource(int amount, Key key, ResourceFactory &factory)
      : amount_{amount},
        key_{key},
        factory_{&factory} {}

  int amount_{};
  Key key_{};
  ResourceFactory *factory_{}; // TODO: Re-think ownership, maybe a weak_ptr?
};

class ResourceFactory {

public:
  template <typename R, typename... Args>
  [[nodiscard]] constexpr auto make(int amount, Args &&...args) -> Resource {
    std::pair<std::vector<R> &, size_t> slot = findSlot<R>();
    slot.first.emplace_back(std::forward<Args>(args)...);
    auto const in_container_index = slot.first.size() - 1;

    return Resource{amount,
                    Resource::Key{
                        .type_index = slot.second,
                        .in_container_index = in_container_index,
                    },
                    *this};
  }

private:
  using ResourceData =
      std::variant<ResourceData_A, ResourceData_B, ResourceData_C>;

  template <typename R, size_t I = 0>
  static consteval auto resource_index() -> size_t {
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

  template <typename R>
  constexpr auto findSlot() -> std::pair<std::vector<R> &, size_t> {
    constexpr auto type_index = resource_index<R>();
    if constexpr (type_index == resource_index<ResourceData_A>()) {
      return {as, type_index};
    } else if constexpr (type_index == resource_index<ResourceData_B>()) {
      return {bs, type_index};
    } else if constexpr (type_index == resource_index<ResourceData_C>()) {
      return {cs, type_index};
    } else {
      // This cannot happen, because resource_index has already handled it.
      throw "absurd: resource container not found";
    }
  }

  friend Resource;

  std::vector<ResourceData_A> as{};
  std::vector<ResourceData_B> bs{};
  std::vector<ResourceData_C> cs{};
};

template <typename R> constexpr auto Resource::is() -> bool {
  return key_.type_index == ResourceFactory::resource_index<R>();
}

// PRE: Resource holds an R, otherwise this is UB.
template <typename R> constexpr auto Resource::as() -> R & {
  std::pair<std::vector<R> &, size_t> slot = factory_->findSlot<R>();
  return slot.first[key_.in_container_index];
}

auto main(int, char *[]) -> int {
  ResourceFactory factory{};

  std::cout << "R1:\n";
  auto r1 = factory.make<ResourceData_A>(100, 42);
  std::cout << r1.is<ResourceData_A>() << std::endl;
  std::cout << r1.is<ResourceData_B>() << std::endl;
  std::cout << r1.is<ResourceData_C>() << std::endl;
  std::cout << "amount: " << r1.amount() << std::endl;
  std::cout << "id: " << r1.as<ResourceData_A>().id << std::endl;

  std::cout << "\n\nR2:\n";

  auto r2 = factory.make<ResourceData_B>(200, "foo");
  std::cout << r2.is<ResourceData_A>() << std::endl;
  std::cout << r2.is<ResourceData_B>() << std::endl;
  std::cout << r2.is<ResourceData_C>() << std::endl;
  std::cout << "amount: " << r2.amount() << std::endl;
  std::cout << "name: " << r2.as<ResourceData_B>().name << std::endl;

  std::cout << "\n\nR3:\n";

  auto r3 = factory.make<ResourceData_A>(300, 666);
  std::cout << r3.is<ResourceData_A>() << std::endl;
  std::cout << r3.is<ResourceData_B>() << std::endl;
  std::cout << r3.is<ResourceData_C>() << std::endl;
  std::cout << "amount: " << r3.amount() << std::endl;
  std::cout << "id: " << r3.as<ResourceData_A>().id << std::endl;

  return 0;
}
