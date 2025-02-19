#include <algorithm>
#include <iostream>
#include <iterator>
#include <optional>
#include <utility>
#include <vector>

namespace ivec {

template <typename T, size_t N> class indexed_vector {
private:
  std::vector<T> values_{};
  constexpr explicit indexed_vector(std::vector<T> values)
      : values_{std::move(values)} {}

  template <typename R, size_t M> friend class indexed_vector;

  template <typename R>
  friend constexpr auto make_empty() -> indexed_vector<R, 0>;

  template <typename R, size_t M>
  friend constexpr auto from_vector(std::vector<R> values)
      -> std::optional<indexed_vector<R, M>>;

public:
  constexpr auto pushed_back(T value) && -> indexed_vector<T, N + 1> {
    values_.push_back(std::move(value));
    return indexed_vector<T, N + 1>{std::move(values_)};
  }

  template <size_t M>
  constexpr auto
  appended(indexed_vector<T, M> rhs) && -> indexed_vector<T, N + M> {
    values_.reserve(values_.size() + rhs.values_.size());
    std::ranges::move(rhs.values_, std::back_inserter(values_));
    return indexed_vector<T, N + M>{std::move(values_)};
  }

  constexpr auto front() const & -> T const &
    requires(N > 0)
  {
    return values_.front();
  }

  constexpr auto into() && -> std::vector<T> { return std::move(values_); }
};

template <typename T> constexpr auto make_empty() -> indexed_vector<T, 0> {
  return indexed_vector<T, 0>{std::vector<T>{}};
}

template <typename T, size_t N>
constexpr auto from_vector(std::vector<T> values)
    -> std::optional<indexed_vector<T, N>> {
  if (values.size() == N) {
    return std::optional{indexed_vector<T, N>{values}};
  } else {
    return std::nullopt;
  }
}

}; // namespace ivec

auto main() -> int {
  auto x = ivec::make_empty<int>();
  // std::cout << x.front() << std::endl; // x is empty => FAIL

  auto const y = std::move(x).appended(ivec::make_empty<int>().pushed_back(10));
  std::cout << y.front() << std::endl; // y is not empty => GOOD

  // expects 3 at compile-time and got a 3 at run-time => GOOD
  return ivec::from_vector<int, 3>({1, 2, 3})
      .transform([](auto &&v) {
        return static_cast<int>(std::ssize(std::move(v).into()));
      })
      .value_or(-1);
}
