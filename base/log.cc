extern "C" {
#include <stdio.h>
#include <unistd.h>
}

#include "base/enumarray.h"
#include "base/log.h"

namespace base {

// Log-writing implementation.
// ===========================

// TODO implement logging to files

namespace {

// TODO: read logging config from proto?
constexpr int log_stderr = 1;
constexpr bool log_color = true;

constexpr EnumArray<LogLevel, char, 6> level_keys = {
  { LogLevel::VERBOSE, 'V' },
  { LogLevel::DEBUG, 'D' },
  { LogLevel::INFO, 'I' },
  { LogLevel::WARNING, 'W' },
  { LogLevel::ERROR, 'E' },
  { LogLevel::FATAL, 'F' },
};

constexpr EnumArray<LogLevel, const char*, 6> color_codes = {
  { LogLevel::VERBOSE, "\x1b[30;1m" },
  { LogLevel::DEBUG, "\x1b[30;1m" },
  { LogLevel::INFO, "\x1b[37m" },
  { LogLevel::WARNING, "\x1b[37;1m" },
  { LogLevel::ERROR, "\x1b[31;1m" },
  { LogLevel::FATAL, "\x1b[31;1m" },
};

const char* const color_code_off = "\x1b[0m";

class LogWriter {
 public:
  LogWriter() {
    color_ = log_color && isatty(fileno(stderr));
  }

  void Write(LogLevel level, const std::string& message) {
    if (static_cast<int>(level) >= log_stderr) {
      if (color_)
        fprintf(stderr, "%s%c %s%s\n", color_codes[level], level_keys[level], message.c_str(), color_code_off);
      else
        fprintf(stderr, "%c %s\n", level_keys[level], message.c_str());
    }
  }

 private:
  bool color_;
};

LogWriter& writer() {
  static thread_local LogWriter writer;
  return writer;
}

} // unnamed namespace

// Log-writing API.
// ================

bool Logger::Enabled(LogLevel level) {
  return static_cast<int>(level) >= log_stderr;
}

void Logger::LogMessage(LogLevel level, const std::string& message) {
  writer().Write(level, message);
}

} // namespace base
