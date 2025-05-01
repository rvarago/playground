#include <cstddef>
#include <iostream>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace utils {

template <typename Variant, typename T, size_t I = 0>
consteval auto variant_index_v() -> size_t {
  static_assert(I < std::variant_size_v<Variant>, "T is not in Variant");

  if constexpr (std::is_same_v<T, std::variant_alternative_t<I, Variant>>) {
    // TODO: Guard against duplicated T.
    return I;
  } else {
    return variant_index_v<Variant, T, I + 1>();
  }
}
} // namespace utils



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
  // TODO: Maybe constrain these with concepts for nicer error-messages?
  template <typename R> [[nodiscard]] constexpr auto is() -> bool;

  template <typename R> [[nodiscard]] constexpr auto as() -> R &;

  [[nodiscard]] auto amount() -> int { return amount_; }

private:
  friend ResourceFactory;

  struct Key {
    size_t to_container_index{};
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
    auto [container, to_container_index] = findContainer<R>();
    container.emplace_back(std::forward<Args>(args)...);
    auto const in_container_index = container.size() - 1;

    return Resource{amount,
                    Resource::Key{
                        .to_container_index = to_container_index,
                        .in_container_index = in_container_index,
                    },
                    *this};
  }

private:
  using ResourceData =
      std::variant<ResourceData_A, ResourceData_B, ResourceData_C>;

  template <typename R>
  static constexpr auto container_index_of =
      utils::variant_index_v<ResourceData, R>();

  template <typename R>
  constexpr auto findContainer() -> std::pair<std::vector<R> &, size_t> {
    constexpr auto index = container_index_of<R>;
    if constexpr (index == container_index_of<ResourceData_A>) {
      return {as, index};
    } else if constexpr (index == container_index_of<ResourceData_B>) {
      return {bs, index};
    } else if constexpr (index == container_index_of<ResourceData_C>) {
      return {cs, index};
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
  return key_.to_container_index == ResourceFactory::container_index_of<R>;
}

// PRE: Resource holds an R, otherwise UB ensues.
template <typename R> constexpr auto Resource::as() -> R & {
  auto [container, _] = factory_->findContainer<R>();
  return container[key_.in_container_index];
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
