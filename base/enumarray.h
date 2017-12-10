#ifndef BASE_ENUMARRAY_H_
#define BASE_ENUMARRAY_H_

#include <initializer_list>
#include <utility>

namespace base {

/**
 * An array for mapping (dense) enumeration constants to a type.
 *
 * This type is expected to be used in constexpr declarations, in
 * which case it will turn into the raw bytes of an array of \p T of
 * length \p N.
 *
 * \p N should be one more than the highest-valued constant of \p
 * Enum. Including higher values in the array initializer will be
 * detected at compile time, but looking them up will not.
 */
template <typename Enum, typename T, int N>
struct EnumArray {
  /** Array storing the items. */
  T items[N];
  /**
   * Constructs a new array from a list of { constant, value } pairs.
   *
   * If the \p def argument is provided, uninitialized elements will
   * be set to that value.
   */
  constexpr EnumArray(std::initializer_list<std::pair<Enum, T>> data, T def=T()) : items{def} {
    for (int i = 1; i < N; i++)
      items[i] = def;
    for (auto&& item : data) {
      if (static_cast<int>(item.first) >= N)
        throw "EnumArray index larger than declared size";
      items[static_cast<int>(item.first)] = item.second;
    }
  }
  /** Looks up the value corresponding to an enum constant in the array. */
  constexpr T operator[](Enum key) const {
    return items[static_cast<int>(key)];
  }
  /** Returns the (compile-time constant) size of the array. */
  constexpr int size() const { return N; }
};

} // namespace base

#endif // BASE_ENUMARRAY_H_

// Local Variables:
// mode: c++
// End:
