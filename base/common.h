/** \file
 * Common utilities used by everything.
 */

#ifndef BASE_COMMON_H_
#define BASE_COMMON_H_

#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>

/** Deletes the default copy constructor and assignment operators of \p TypeName. */
#define DISALLOW_COPY(TypeName) \
  TypeName(const TypeName&) = delete; \
  TypeName& operator=(const TypeName&) = delete

namespace base {

/** A variant of std::unique_ptr that can also hold a non-owned pointer. */
template <typename T>
class optional_ptr {
 public:
  using pointer = T*;
  using element_type = T;

  optional_ptr() noexcept : ptr_(nullptr), owned_(false) {}
  optional_ptr(std::nullptr_t) noexcept : ptr_(nullptr), owned_(false) {}

  template <typename U>
  explicit optional_ptr(U* ptr) noexcept : ptr_(ptr), owned_(false) {}

  template <typename U>
  explicit optional_ptr(std::unique_ptr<U> ptr) noexcept : ptr_(ptr.release()), owned_(true) {}

  optional_ptr(optional_ptr&& other) noexcept : ptr_(other.ptr_), owned_(other.owned_) {
    other.ptr_ = nullptr; other.owned_ = false;
  }

  template <typename U>
  optional_ptr(optional_ptr<U>&& other) noexcept : ptr_(other.ptr_), owned_(other.owned_) {
    other.ptr_ = nullptr; other.owned_ = false;
  }

  DISALLOW_COPY(optional_ptr);

  ~optional_ptr() { Free(); }

  optional_ptr& operator=(std::nullptr_t) noexcept {
    Free();
    ptr_ = nullptr; owned_ = false;
    return *this;
  }

  template <typename U>
  optional_ptr& operator=(U* ptr) noexcept {
    Free();
    ptr_ = ptr; owned_ = false;
    return *this;
  }

  template <typename U>
  optional_ptr& operator=(std::unique_ptr<U> ptr) noexcept {
    Free();
    ptr_ = ptr.release(); owned_ = true;
    return *this;
  }

  optional_ptr& operator=(optional_ptr&& other) noexcept {
    Free();
    ptr_ = other.ptr_; owned_ = other.owned_;
    other.ptr_ = nullptr; other.owned_ = false;
    return *this;
  }

  explicit operator bool() const noexcept { return ptr_; }
  T* get() const noexcept { return ptr_; }

  T& operator*() const noexcept { return *ptr_; }
  T* operator->() const noexcept { return ptr_; }

  bool owned() const noexcept { return owned_; }

 private:
  template <typename U>
  friend class optional_ptr;

  T* ptr_;
  bool owned_;

  void Free() {
    if (owned_)
      delete ptr_;
  }
};

template <typename T>
optional_ptr<T> own(std::unique_ptr<T> ptr) { return optional_ptr<T>(std::move(ptr)); }

template <typename T>
optional_ptr<T> borrow(T* ptr) { return optional_ptr<T>(ptr); }

template <typename T, typename... Args>
optional_ptr<T> make_owned(Args&&... args) {
  return own(std::make_unique<T>(std::forward<Args>(args)...));
}

} // namespace base

#endif // BASE_COMMON_H_

// Local Variables:
// mode: c++
// End:
