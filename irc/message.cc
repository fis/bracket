#include <algorithm>
#include <cctype>
#include <cstring>

#include "irc/message.h"

namespace irc {

// TODO: consider leveraging RE2 for some of this

Message::Message(std::initializer_list<const char*> contents, const char* prefix) {
  if (prefix)
    prefix_.append(prefix);

  const char* const* p = contents.begin();
  const char* const* end = contents.end();

  if (p != end) {
    command_.append(*p);
    ++p;
  }

  while (p != end) {
    args_.emplace_back(*p);
    ++p;
  }
}

bool Message::Parse(const unsigned char* data, std::size_t count) {
  auto* p = reinterpret_cast<const char*>(data);
  std::size_t left = count;

  // parse prefix

  prefix_.clear();
  if (left > 0 && *p == ':') {
    ++p;
    --left;

    auto* d = static_cast<const char*>(std::memchr(p, ' ', left));
    if (!d)
      return false;

    std::size_t prefix_len = d - p;
    prefix_.append(p, prefix_len);
    p += prefix_len;
    left -= prefix_len;
  }

  // skip any extra leading whitespace

  while (left > 0 && *p == ' ') {
    ++p;
    --left;
  }

  // parse command

  command_.clear();
  {
    std::size_t command_len = 0;
    while (command_len < left && p[command_len] != ' ')
      ++command_len;
    if (command_len == 0)
      return false;
    command_.append(p, command_len);
    p += command_len;
    left -= command_len;
  }

  // parse any arguments

  args_.clear();
  while (left > 0) {
    while (left > 0 && *p == ' ') {
      ++p;
      --left;
    }
    if (left == 0)
      break;

    if (*p == ':') {
      args_.emplace_back(p+1, left-1);
      return true;
    }

    std::size_t arg_len = 0;
    while (arg_len < left && p[arg_len] != ' ')
      ++arg_len;
    args_.emplace_back(p, arg_len);
    p += arg_len;
    left -= arg_len;
  }

  return true;
}

std::size_t Message::Write(unsigned char* buffer, std::size_t size) const {
  std::size_t at = 0;

  if (!prefix_.empty()) {
    if (at < size)
      buffer[at] = ':';
    ++at;

    if (at < size)
      std::memcpy(buffer + at, prefix_.data(), std::min(prefix_.size(), size - at));
    at += prefix_.size();

    if (at < size)
      buffer[at] = ' ';
    ++at;
  }

  if (at < size)
    std::memcpy(buffer + at, command_.data(), std::min(command_.size(), size - at));
  at += command_.size();

  for (std::size_t arg_idx = 0, arg_count = args_.size();
       arg_idx < arg_count;
       ++arg_idx) {
    const std::string& arg = args_[arg_idx];

    if (at < size)
      buffer[at] = ' ';
    ++at;

    if (arg_idx == arg_count - 1 && memchr(arg.data(), ' ', arg.size())) {
      if (at < size)
        buffer[at] = ':';
      ++at;
    }

    if (at < size)
      std::memcpy(buffer + at, arg.data(), std::min(arg.size(), size - at));
    at += arg.size();
  }

  return at;
}

std::string_view Message::prefix_nick() const {
  // TODO: precompute these when parsing, just return direct views

  const char* prefix = prefix_.data();
  std::size_t len = prefix_.size();

  const char* ex = static_cast<const char*>(memchr(prefix, '!', len));
  if (!ex)
    return std::string_view();
  const char* at = static_cast<const char*>(memchr(ex + 1, '@', len - (ex - prefix) - 1));
  if (!at)
    return std::string_view();

  if (ex == prefix || at == ex + 1 || at == prefix + len - 1)
    return std::string_view();

  return std::string_view(prefix, ex - prefix);
}

bool Message::EqualArg(const char* a, const char* b) {
  for (; *a && *b; ++a, ++b) {
    if (std::tolower((unsigned char)*a) != std::tolower((unsigned char)*b))
      return false;
  }
  return !*a && !*b;
}

} // namespace irc
