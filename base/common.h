/** \file
 * Common utilities used by everything.
 */

#ifndef BASE_COMMON_H_
#define BASE_COMMON_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <type_traits>
#include <unordered_set>
#include <utility>

/** Deletes the default copy constructor and assignment operators of \p TypeName. */
#define DISALLOW_COPY(TypeName) \
  TypeName(const TypeName&) = delete; \
  TypeName& operator=(const TypeName&) = delete

namespace base {

/** A variant of std::unique_ptr that can also hold a non-owned pointer. */
template <typename T, bool aligned = (alignof (T) > 1)>
class optional_ptr {
 public:
  using pointer = T*;
  using element_type = T;

  optional_ptr() noexcept { set(nullptr, false); }
  optional_ptr(std::nullptr_t) noexcept { set(nullptr, false); }

  template <typename U>
  explicit optional_ptr(U* ptr) noexcept { set(ptr, false); }

  template <typename U>
  explicit optional_ptr(std::unique_ptr<U> ptr) noexcept { set(ptr.release(), true); }

  optional_ptr(optional_ptr&& other) noexcept {
    bool owned = other.owned(); set(other.release(), owned);
  }

  template <typename U>
  optional_ptr(optional_ptr<U>&& other) noexcept {
    bool owned = other.owned(); set(other.release(), owned);
  }

  DISALLOW_COPY(optional_ptr);

  ~optional_ptr() { Free(); }

  optional_ptr& operator=(std::nullptr_t) noexcept {
    Free(); set(nullptr, false); return *this;
  }

  template <typename U>
  optional_ptr& operator=(U* ptr) noexcept {
    Free(); set(ptr, false); return *this;
  }

  template <typename U>
  optional_ptr& operator=(std::unique_ptr<U> ptr) noexcept {
    Free(); set(ptr.release(), true); return *this;
  }

  optional_ptr& operator=(optional_ptr&& other) noexcept {
    Free(); bool owned = other.owned(); set(other.release(), owned); return *this;
  }

  template <typename U>
  optional_ptr& operator=(optional_ptr<U>&& other) noexcept {
    Free(); bool owned = other.owned(); set(other.release(), owned); return *this;
  }

  explicit operator bool() const noexcept { return ptr_; }
  T* get() const noexcept { return ptr_; }
  T* release() noexcept { auto p = get(); set(nullptr, false); return p; }

  T& operator*() const noexcept { return *get(); }
  T* operator->() const noexcept { return get(); }

  bool owned() const noexcept { return owned_; }

 private:
  T* ptr_;
  bool owned_;

  void set(T* ptr, bool owned) { ptr_ = ptr; owned_ = owned; }

  void Free() {
    if (owned())
      delete get();
  }
};

/** A specialization of optional_ptr for types that the alignment trick works on. */
template <typename T>
class optional_ptr<T, true> {
 public:
  using pointer = T*;
  using element_type = T;

  optional_ptr() noexcept { set(nullptr, false); }
  optional_ptr(std::nullptr_t) noexcept { set(nullptr, false); }

  template <typename U>
  explicit optional_ptr(U* ptr) noexcept { set(ptr, false); }

  template <typename U>
  explicit optional_ptr(std::unique_ptr<U> ptr) noexcept { set(ptr.release(), true); }

  optional_ptr(optional_ptr&& other) noexcept {
    bool owned = other.owned(); set(other.release(), owned);
  }

  template <typename U>
  optional_ptr(optional_ptr<U>&& other) noexcept {
    bool owned = other.owned(); set(other.release(), owned);
  }

  DISALLOW_COPY(optional_ptr);

  ~optional_ptr() { Free(); }

  optional_ptr& operator=(std::nullptr_t) noexcept {
    Free(); set(nullptr, false); return *this;
  }

  template <typename U>
  optional_ptr& operator=(U* ptr) noexcept {
    Free(); set(ptr, false); return *this;
  }

  template <typename U>
  optional_ptr& operator=(std::unique_ptr<U> ptr) noexcept {
    Free(); set(ptr.release(), true); return *this;
  }

  optional_ptr& operator=(optional_ptr&& other) noexcept {
    Free(); bool owned = other.owned(); set(other.release(), owned); return *this;
  }

  template <typename U>
  optional_ptr& operator=(optional_ptr<U>&& other) noexcept {
    Free(); bool owned = other.owned(); set(other.release(), owned); return *this;
  }

  explicit operator bool() const noexcept { return data_; }
  T* get() const noexcept { return reinterpret_cast<T*>(data_ & ~static_cast<std::uintptr_t>(1)); }
  T* release() noexcept { auto p = get(); set(nullptr, false); return p; }

  T& operator*() const noexcept { return *get(); }
  T* operator->() const noexcept { return get(); }

  bool owned() const noexcept { return data_ & 1; }

 private:
  std::uintptr_t data_;

  void set(T* ptr, bool owned) {
    data_ = reinterpret_cast<std::uintptr_t>(ptr) | static_cast<std::uintptr_t>(owned ? 1 : 0);
  }

  void Free() {
    if (owned())
      delete get();
  }
};

// optional_ptr utilities

template <typename T, bool aligned>
bool operator==(const optional_ptr<T, aligned>& a, const optional_ptr<T, aligned>& b) { return a.get() == b.get(); }
template <typename T, bool aligned>
bool operator!=(const optional_ptr<T, aligned>& a, const optional_ptr<T, aligned>& b) { return a.get() != b.get(); }

template <typename T>
optional_ptr<T> own(std::unique_ptr<T> ptr) { return optional_ptr<T>(std::move(ptr)); }

template <typename T>
optional_ptr<T> borrow(T* ptr) { return optional_ptr<T>(ptr); }

template <typename T, typename... Args>
optional_ptr<T> make_owned(Args&&... args) {
  return own(std::make_unique<T>(std::forward<Args>(args)...));
}

template <typename T>
using optional_set = std::unordered_set<optional_ptr<T>>;

} // namespace base

// hash specialization for optional_ptr

template <typename T>
struct std::hash<base::optional_ptr<T>> {
  std::size_t operator()(const base::optional_ptr<T>& ptr) const noexcept {
    return std::hash<T*>()(ptr.get());
  }
};

#endif // BASE_COMMON_H_

// Local Variables:
// mode: c++
// End:
