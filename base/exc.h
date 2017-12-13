/** \file
 * Base class for exceptions, with `errno` handling.
 */

#ifndef BASE_EXC_H_
#define BASE_EXC_H_

#include <exception>
#include <string>
#include <system_error>

namespace base {

/** Base exception with a string message and an optional POSIX errno value. */
class Exception : public std::exception {
 public:
  /** Constructs a new exception with the provided message. */
  explicit Exception(const std::string& what, int errno_value = 0) : what_(what), errno_(errno_value) {
    if (errno_value != 0) {
      what_ += " [";
      what_ += std::to_string(errno_value);
      what_ += ": ";
      what_ += std::generic_category().message(errno_value);
      what_ += ']';
    }
  }
  virtual ~Exception() {}
  /** Returns the detail message. */
  const char* what() const noexcept override { return what_.c_str(); }
 private:
  std::string what_;
  int errno_;
};

} // namespace base

#endif // BASE_EXC_H_

// Local Variables:
// mode: c++
// End:
