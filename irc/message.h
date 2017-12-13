/** \file
 * IRC protocol message type.
 */

#ifndef IRC_MESSAGE_H_
#define IRC_MESSAGE_H_

#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace irc {

/** IRC protocol message. */
class Message {
 public:
  /** Constructs an empty message. */
  Message() {}

  /** Constructs a message from C strings. */
  Message(std::initializer_list<const char*> contents, const char* prefix = nullptr);

  /**
   * Updates the message contents by parsing an IRC protocol message.
   *
   * The message is expected not to contain any `CR` or `LF`
   * characters. If the function returns `false` (indicating that the
   * message wasn't valid as per the IRC protocol), the contents of
   * the message are unspecified.
   *
   * The validity checks are more relaxed than those in the
   * specification. For example, this function accepts any non-space
   * characters inside the command.
   */
  bool Parse(const char* data, std::size_t count);

  /**
   * Updates the message contents by parsing a 0-terminated string.
   *
   * This is a convenience method for Parse(const char*, std::size_t).
   */
  bool Parse(const char* data) {
    return Parse(data, std::strlen(data));
  }

  /**
   * Serializes the message to \p buffer.
   *
   * This is the inverse of Parse(), so the result does not include
   * the CR-LF delimiter.
   *
   * At most \p size bytes will be written, but the return value is
   * the "natural" size of the message (`snprintf` style). If \p size
   * is 0, \p buffer can be `nullptr`.
   *
   * No special validity checks are done. The caller is responsible
   * for making sure the message is valid for the IRC protocol. In
   * particular, this means only the last argument may contain a
   * space.
   */
  std::size_t Write(char* buffer, std::size_t size) const;

  /**
   * Returns the size needed to write the message. Equivalent to
   * `Write(nullptr, 0)`.
   */
  std::size_t WriteSize() const {
    return Write(nullptr, 0);
  }

  /** Clears all data, making this an empty message. */
  void Clear() {
    prefix_.clear();
    command_.clear();
    args_.clear();
  }

  /** Returns the message prefix, which may be empty. */
  const std::string& prefix() const { return prefix_; }
  /** Returns the command, which is only empty for an empty message. */
  const std::string& command() const { return command_; }
  /** Returns the list of arguments. */
  const std::vector<std::string>& args() const { return args_; }
  /** Returns the number of arguments. */
  int nargs() const { return args_.size(); }
  /** Returns the contents of the argument \p at. */
  const std::string& arg(int at) const { return args_.at(at); }

  /** Returns the nick portion of the prefix, if it's in the `nick!user@host` form. */
  std::string_view prefix_nick() const;

  /** Returns true if the command field matches (ASCII-case-insensitive) \p test. */
  bool command_is(const char* test) const { return EqualArg(command_.data(), test); }
  /** Returns true if argument \p n exists and matches (ASCII-case-insensitive) \p test. */
  bool arg_is(unsigned n, const char* test) const { return n < args_.size() && EqualArg(args_[n].data(), test); }

 private:
  std::string prefix_;
  std::string command_;
  std::vector<std::string> args_;

  static bool EqualArg(const char* a, const char* b);
};

} // namespace irc

#endif // IRC_MESSAGE_H_

// Local Variables:
// mode: c++
// End:
