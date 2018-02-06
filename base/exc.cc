#include <system_error>

#include "base/exc.h"

namespace base {

namespace internal {

class cstr_error : public error {
 public:
  cstr_error(const char* what) : what_(what) {}
  void format(std::string* str) const override;
  void format(std::ostream* str) const override;
 private:
  const char* what_;
};

class string_error : public error {
 public:
  string_error(const std::string& what) : what_(what) {}
  void format(std::string* str) const override;
  void format(std::ostream* str) const override;
 private:
  std::string what_;
};

void cstr_error::format(std::string* str) const { *str += what_; }
void cstr_error::format(std::ostream* str) const { *str << what_; }
void string_error::format(std::string* str) const { *str += what_; }
void string_error::format(std::ostream* str) const { *str << what_; }

} // namespace internal

error_ptr make_error(const char* what) {
  return std::make_unique<internal::cstr_error>(what);
}

error_ptr make_error(const std::string& what) {
  return std::make_unique<internal::string_error>(what);
}

void os_error::format(std::string* str) const {
  if (what_) *str += what_;
  if (errno_) {
    if (what_) *str += " [";
    *str += std::to_string(errno_); *str += ": "; *str += std::generic_category().message(errno_);
    if (what_) *str += ']';
  }
}

void os_error::format(std::ostream* str) const {
  if (what_) *str << what_;
  if (errno_) {
    if (what_) *str << " [";
    *str << errno_ << ": " << std::generic_category().message(errno_);
    if (what_) *str << "]";
  }
}

void file_error::format(std::string* str) const {
  *str += path_; *str += ": "; error_.format(str);
}

void file_error::format(std::ostream* str) const {
  *str << path_ << ": "; error_.format(str);
}

} // namespace base
