/** \file
 * Utilities for errors and exceptions.
 */

#ifndef BASE_EXC_H_
#define BASE_EXC_H_

#include <exception>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <variant>

#include "base/common.h"

namespace base {

/** Base type for formattable error objects. */
struct error {
  /** Appends the formatted error message to string \p str. */
  virtual void format(std::string* str) const = 0;
  /** Appends the formatted error message to stream \p str. */
  virtual void format(std::ostream* str) const = 0;
  virtual ~error() = default;
};

/** Outputs the formatted error message to a stream. */
inline std::ostream& operator<<(std::ostream& os, const error& err) { err.format(&os); return os; }

/** Constructs a new simple error object from a static string. */
std::unique_ptr<error> make_error(const char* what);
/** Constructs a new simple error object with a copy of a string object. */
std::unique_ptr<error> make_error(const std::string& what);

/**
 * Error object type for system errors, optionally with errno.
 *
 * Normally you would use the make_os_error() or maybe_os_error() convenience functions, though
 * `base::os_error(...).format(...)` is also a reasonable fragment.
 */
class os_error : public error {
 public:
  /** Constructs a new system error with an explanation and an optional errno. */
  explicit os_error(const char* what, int errno_value = 0) : what_(what), errno_(errno_value) {}
  /** Constructs a new system error with just an errno value. */
  explicit os_error(int errno_value) : errno_(errno_value) {}

  void format(std::string* str) const override;
  void format(std::ostream* str) const override;

 private:
  const char* what_ = nullptr;
  int errno_ = 0;
};

/** Constructs a new os_error with one of the constructors. */
template <typename... Args>
inline std::unique_ptr<os_error> make_os_error(Args&&... args) {
  return std::make_unique<os_error>(std::forward<Args>(args)...);
}

/**
 * Error object type for system errors that related to a path name.
 *
 * \sa os_error
 */
class file_error : public error {
 public:
  /** Constructs a new file system error related to \p path. */
  file_error(const char* path, const char* what, int errno_value = 0) : error_(what, errno_value), path_(path) {}
  /** \overload */
  file_error(const std::string& path, const char* what, int errno_value = 0) : error_(what, errno_value), path_(path) {}
  /** \overload */
  file_error(const char* path, int errno_value) : error_(errno_value), path_(path) {}
  /** \overload */
  file_error(const std::string& path, int errno_value) : error_(errno_value), path_(path) {}

  void format(std::string* str) const override;
  void format(std::ostream* str) const override;

 private:
  os_error error_;
  std::string path_;
};

/** Constructs a new file_error with one of the constructors. */
template <typename... Args>
inline std::unique_ptr<file_error> make_file_error(Args&&... args) {
  return std::make_unique<file_error>(std::forward<Args>(args)...);
}

/**
 * Variant type containing either a `unique_ptr<T>` or an error.
 *
 * **Note**: retrieving the contents (via the #ptr() or #error() methods) causes this object to lose
 * ownership of them, leaving behind null pointers.
 *
 * Objects of this type are generally constructed only via the convenience functions starting with
 * `maybe_`, such as maybe_ok() and maybe_error().
 */
template <typename T>
class maybe_ptr {
 public:
  /** Corresponding raw pointer type. */
  using pointer = T*;
  /** Element type this pointer (possibly) points at. */
  using element_type = T;

  /** Move constructor. */
  maybe_ptr(maybe_ptr&& other) : value_(std::move(other.value_)) {}

  /** Converting move constructor. */
  template <typename U>
  maybe_ptr(maybe_ptr<U>&& other) {
    // TODO: try to figure out some way to get maybe_ptr<X> to maybe_ptr<Y> to work
    if (other.value_.index() == 0)
      value_ = std::variant<std::unique_ptr<T>, std::unique_ptr<base::error>>(std::in_place_index<0>, std::move(std::get<0>(other.value_)));
    else
      value_ = std::variant<std::unique_ptr<T>, std::unique_ptr<base::error>>(std::in_place_index<1>, std::move(std::get<1>(other.value_)));
  }

  DISALLOW_COPY(maybe_ptr);

  /** Returns `true` if this object holds a `T` instead of an error. */
  bool ok() const noexcept { return value_.index() == 0; }

  /** Takes ownership of the contained `T` object. */
  std::unique_ptr<T> ptr() { return std::move(std::get<0>(value_)); }
  /** Takes ownership of the contained error object. */
  std::unique_ptr<base::error> error() { return std::move(std::get<1>(value_)); }

 private:
  template <typename U> friend class maybe_ptr;
  template <typename T2, typename U> friend maybe_ptr<T2> maybe_ok_from(std::unique_ptr<U> ptr);
  template <typename T2> friend maybe_ptr<T2> maybe_error(std::unique_ptr<base::error> error);

  std::variant<std::unique_ptr<T>, std::unique_ptr<base::error>> value_;

  struct ok_tag { explicit ok_tag() = default; };
  maybe_ptr(ok_tag, std::unique_ptr<T> value) : value_(std::in_place_index<0>, std::move(value)) {}

  struct error_tag { explicit error_tag() = default; };
  maybe_ptr(error_tag, std::unique_ptr<base::error> error) : value_(std::in_place_index<1>, std::move(error)) {}
};

/** Converts a regular `std::unique_ptr<T>` to a `maybe_ptr<T>`. */
template <typename T, typename U>
inline maybe_ptr<T> maybe_ok_from(std::unique_ptr<U> ptr) {
  return maybe_ptr<T>(typename maybe_ptr<T>::ok_tag(), std::move(ptr));
}

/** Constructs a new `T` and returns `maybe_ptr<T>` for it. */
template <typename T, typename... Args>
inline maybe_ptr<T> maybe_ok(Args&&... args) {
  return maybe_ok_from<T>(std::make_unique<T>(std::forward<Args>(args)...));
}

/** Converts a regular `std::unique_ptr<error>` to a `maybe_ptr` of any type holding that error. */
template <typename T>
inline maybe_ptr<T> maybe_error(std::unique_ptr<error> error) {
  return maybe_ptr<T>(typename maybe_ptr<T>::error_tag(), std::move(error));
}

/** Constructs a new simple error and returns it as a `maybe_ptr<T>` of desired type. */
template <typename T>
inline maybe_ptr<T> maybe_error(const char* what) {
  return maybe_error<T>(make_error(what));
}

/** Constructs a new simple error and returns it as a `maybe_ptr<T>` of desired type. */
template <typename T>
inline maybe_ptr<T> maybe_error(const std::string& what) {
  return maybe_error<T>(make_error(what));
}

/** Constructs a new os_error and returns it as a `maybe_ptr<T>` of desired type. */
template <typename T, typename... Args>
inline maybe_ptr<T> maybe_os_error(Args&&... args) {
  return maybe_error<T>(make_os_error(std::forward<Args>(args)...));
}

/** Constructs a new file_error and returns it as a `maybe_ptr<T>` of desired type. */
template <typename T, typename... Args>
inline maybe_ptr<T> maybe_file_error(Args&&... args) {
  return maybe_error<T>(make_file_error(std::forward<Args>(args)...));
}

/**
 * Result type for IO operations.
 *
 * Represents three different kinds of results:
 * - #ok(): no errors, holds a `std::size_t` result (possibly 0 if non-blocking).
 * - #at_eof(): no errors, but an EOF condition resulted in no data.
 * - #failed(): an error (other than a would-block case) occurred.
 */
class io_result {
 private:
  struct eof_tag { explicit eof_tag() = default; };

 public:
  /** Returns an #ok() result with size \p size. */
  static io_result ok(std::size_t size) { return io_result(size); }
  /** Returns an #at_eof() result. */
  static io_result eof() { return io_result(eof_tag()); }
  /** Returns a #failed() result with the given error object. */
  static io_result error(std::unique_ptr<error> error) { return io_result(std::move(error)); }
  /** Returns a #failed() result with a new os_error object. */
  template <typename... Args>
  static io_result os_error(Args&&... args) { return io_result(base::make_os_error(std::forward<Args>(args)...)); }
  /** Returns a #failed() result with a new file_error object. */
  template <typename... Args>
  static io_result file_error(Args&&... args) { return io_result(base::make_file_error(std::forward<Args>(args)...)); }

  /** Returns `true` for a successful (or non-blocking would-block) result. */
  bool ok() const noexcept { return std::holds_alternative<std::size_t>(value_); }
  /** Returns `true` for a result representing EOF with no data. */
  bool at_eof() const noexcept { return std::holds_alternative<eof_tag>(value_); }
  /** Returns `true` for a failed result. */
  bool failed() const noexcept { return std::holds_alternative<std::unique_ptr<base::error>>(value_); }

  /** Returns the size of a successful operation, or 0 otherwise. */
  std::size_t size() const noexcept {
    if (ok())
      return std::get<std::size_t>(value_);
    else
      return 0;
  }

  /**
   * Takes ownership of the contained error object, if any.
   *
   * If this was an #ok() result, returns a null pointer. For an #at_eof() result, returns a
   * synthetic error. For a #failed() result, takes ownership of the error it failed with, or
   * returns a placeholder error if the real error has already been extracted.
   */
  std::unique_ptr<base::error> error() noexcept {
    if (ok())
      return nullptr;
    if (at_eof())  // TODO: optional_ptr to use static errors?
      return make_os_error("EOF");
    auto error = std::move(std::get<std::unique_ptr<base::error>>(value_));
    return error ? std::move(error) : base::make_error("error already taken");
  }

 private:
  std::variant<std::size_t, eof_tag, std::unique_ptr<base::error>> value_;

  io_result(std::size_t size) : value_(size) {}
  io_result(eof_tag) : value_(eof_tag()) {}
  io_result(std::unique_ptr<base::error> error) : value_(std::move(error)) {}
};

/** Base exception with a string message and an optional POSIX errno value. */
class Exception : public std::exception {
 public:
  /** Constructs a new exception with the provided message. */
  explicit Exception(const char* what, int errno_value = 0) {
    os_error(what, errno_value).format(&what_);
  }
  explicit Exception(const std::string& what, int errno_value = 0) {
    os_error(what.c_str(), errno_value).format(&what_);
  }
  explicit Exception(const base::error& error) {
    error.format(&what_);
  }

  /** Returns the detail message. */
  const char* what() const noexcept override { return what_.c_str(); }

 private:
  std::string what_;
};

} // namespace base

#endif // BASE_EXC_H_

// Local Variables:
// mode: c++
// End:
