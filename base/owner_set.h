#ifndef BASE_POINTER_SET_H_
#define BASE_POINTER_SET_H_

#include <memory>
#include <unordered_set>
#include <utility>

#include "base/common.h"

namespace base {

/**
 * Set of ownership-holding pointers.
 *
 * `base::owner_set<T>` has more or less the semantics of `std::unordered_set<std::unique_ptr<T>>`,
 * with the exception that it allows for deleting items based on a raw pointer to the item. It also
 * only implements some of the container methods.
 *
 * \tparam T type of contained objects
 */
template<typename T>
class owner_set {
 public:
  /** Constructs an empty set. */
  owner_set() {}
  DISALLOW_COPY(owner_set);

  /** Destroys the set, and all the objects in it. */
  ~owner_set() {
    for (auto&& item : data_)
      delete item;
  }

  /**
   * Inserts a new object to the set, giving it ownership over it.
   *
   * \tparam DT type of inserted object, must be a subclass of \p T
   */
  template<typename DT>
  DT* insert(std::unique_ptr<DT> item) {
    // TODO: exception safety
    DT* owned_item = item.release();
    data_.insert(owned_item);
    return owned_item;
  }

  /** Constructs a new object owned by the set. */
  template<typename... Args>
  T* emplace(Args&&... args) {
    // TODO: exception safety
    T* item = new T(std::forward<Args>(args)...);
    data_.insert(item);
    return item;
  }

  /**
   * Removes the matching item from the set, which causes it to be destroyed.
   *
   * If this method returns `true` (the item used to be owned by the set), the passed-in pointer
   * becomes invalid, and should no longer be used.
   */
  bool erase(T* item) {
    bool found = data_.erase(item) > 0;
    if (found)
      delete item;
    return found;
  }

  /**
   * Claims ownership of the matching item.
   *
   * If the item isn't owned by the set, returns an empty `std::unique_ptr`.
   */
  std::unique_ptr<T> claim(T* item) {
    bool found = data_.erase(item) > 0;
    if (found)
      return std::unique_ptr<T>(item);
    else
      return std::unique_ptr<T>();
  }

  /** Returns an iterator to the beginning of the set. */
  decltype(auto) begin() { return data_.begin(); }
  /** Returns an iterator to the end of the set. */
  decltype(auto) end() { return data_.end(); }

  /** Returns `true` if the set owns no items. */
  bool empty() const noexcept { return data_.empty(); }

 private:
  std::unordered_set<T*> data_;
};

} // namespace base

#endif // BASE_POINTER_SET_H_

// Local Variables:
// mode: c++
// End:
