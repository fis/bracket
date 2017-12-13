/** \file
 * Simple logging facility.
 */

#ifndef BASE_LOG_H_
#define BASE_LOG_H_

#include <cstdint>
#include <string>
#include <sstream>

#include "base/exc.h"

namespace base {

/** Logging levels. */
enum LogLevel {
  /** Verbose logging, usually not even needed for debugging. Not available in optimized builds. */
  VERBOSE = 0,
  /** Debug logging. Not available in optimized builds. */
  DEBUG = 1,
  /** Informational messages that do not necessarily mean anything unexpected has happened. */
  INFO = 2,
  /** Warning messages of slightly unusual activities. */
  WARNING = 3,
  /** Potentially serious error conditions. */
  ERROR = 4,
  /** Invariably fatal errors. Only used if the program will throw an exception. */
  FATAL = 5,
};

/** Logging class. Typically used via the macros. */
class Logger {
 public:
  /** Returns `true` if logging at the specified level is enabled. */
  static bool Enabled(LogLevel level);
  /** Logs a message at the given level. */
  static void LogMessage(LogLevel level, const std::string& message);
};

/** Lowest available logging level in this build. */
constexpr LogLevel kMinAvailableLogLevel =
#ifdef NDEBUG
    LogLevel::INFO
#else
    LogLevel::VERBOSE
#endif
    ;

/** Internal class for the LOG() macro. Logs a message when destroyed. */
template <bool available>
class LogEvent {
 public:
  /** Starts a log event sequence. */
  LogEvent(bool enabled, LogLevel level) : enabled_(enabled), level_(level) {}
  /** Logs the message inserted so far. */
  ~LogEvent() {
    if (available && enabled_)
      Logger::LogMessage(level_, message_.str());
  }

  /** Appends the provided argument to the current log message. */
  template <typename T>
  LogEvent& operator<<(T v) { if (available && enabled_) message_ << v; return *this; }

  LogEvent(const LogEvent&) = delete;
  LogEvent& operator=(const LogEvent&) = delete;

 private:
  /** `true` if logging at this level is enabled. */
  bool enabled_;
  /** Logging level for the message. */
  LogLevel level_;
  /** Collected message so far. */
  std::ostringstream message_;
};

// TODO make operator<< in here not match all the types

/** Template specialization for compile-time-disabled logging levels. */
template <>
class LogEvent<false> {
 public:
  /** Constructs a new no-op LogEvent variant. */
  LogEvent(bool enabled, LogLevel level) {}
  ~LogEvent() {}

  /** Does nothing. */
  template <typename T>
  LogEvent& operator<<(T v) { return *this; }
};

} // namespace base

/**
 * Starts a log event at the given level.
 *
 * Stream messages into the result of this macro to log.
 */
#define LOG(level) \
  ::base::LogEvent<::base::LogLevel::level >= ::base::kMinAvailableLogLevel>( \
    ::base::Logger::Enabled(::base::LogLevel::level), ::base::LogLevel::level)

#define CHECK_message(f,l,e) CHECK_message_(f,l,e)
#define CHECK_message_(f,l,e) f ":" #l ": CHECK(" #e ")"

/** Logs a fatal error and throws an exception if the provided expression is not true. */
#define CHECK(expr) \
  do { \
    if (!(expr)) { \
      ::base::LogEvent<true>(true, ::base::LogLevel::FATAL) << (CHECK_message(__FILE__, __LINE__, expr)); \
      throw ::base::Exception("FATAL: " CHECK_message(__FILE__, __LINE__, expr)); \
    } \
  } while (0)

/** Logs a fatal error and throws an exception. */
#define FATAL(message) \
  do { \
    ::base::LogEvent<true>(true, ::base::LogLevel::FATAL) << message; \
    throw ::base::Exception("FATAL: " message); \
  } while (0)

/** Defines a log formatting function for a custom type. */
#define LOG_TYPE(T, log, arg) \
  template <bool available> \
  ::base::LogEvent<available>& operator<<(::base::LogEvent<available>& log, const T& arg)

#endif // BASE_LOG_H_

// Local Variables:
// mode: c++
// End:
