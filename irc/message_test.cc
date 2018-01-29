#include <cstring>

#include "irc/message.h"
#include "gtest/gtest.h"

namespace irc {

// Parsing of simple formats.

TEST(MessageTest, ParseCommand) {
  Message m;
  bool ok = m.Parse("quit");
  ASSERT_TRUE(ok);
  EXPECT_TRUE(m.prefix().empty());
  EXPECT_EQ(m.command(), "quit");
  EXPECT_EQ(m.nargs(), 0);
}

TEST(MessageTest, ParseCommandAndArgs) {
  Message m;
  bool ok = m.Parse("whois foo bar");
  ASSERT_TRUE(ok);
  EXPECT_TRUE(m.prefix().empty());
  EXPECT_EQ(m.command(), "whois");
  EXPECT_EQ(m.nargs(), 2);
  EXPECT_EQ(m.arg(0), "foo");
  EXPECT_EQ(m.arg(1), "bar");
}

TEST(MessageTest, ParseCommandAndTrailing) {
  Message m;
  bool ok = m.Parse("quit :some message here");
  ASSERT_TRUE(ok);
  EXPECT_TRUE(m.prefix().empty());
  EXPECT_EQ(m.command(), "quit");
  EXPECT_EQ(m.nargs(), 1);
  EXPECT_EQ(m.arg(0), "some message here");
}

TEST(MessageTest, ParseCommandAndArgsAndTrailing) {
  Message m;
  bool ok = m.Parse("whois foo bar :extra stuff");
  ASSERT_TRUE(ok);
  EXPECT_TRUE(m.prefix().empty());
  EXPECT_EQ(m.command(), "whois");
  EXPECT_EQ(m.nargs(), 3);
  EXPECT_EQ(m.arg(0), "foo");
  EXPECT_EQ(m.arg(1), "bar");
  EXPECT_EQ(m.arg(2), "extra stuff");
}

TEST(MessageTest, ParsePrefixedCommand) {
  Message m;
  bool ok = m.Parse(":irc.server quit");
  ASSERT_TRUE(ok);
  EXPECT_EQ(m.prefix(), "irc.server");
  EXPECT_EQ(m.command(), "quit");
  EXPECT_EQ(m.nargs(), 0);
}

TEST(MessageTest, ParsePrefixedCommandAndTrailing) {
  Message m;
  bool ok = m.Parse(":irc.server quit :some message here");
  ASSERT_TRUE(ok);
  EXPECT_EQ(m.prefix(), "irc.server");
  EXPECT_EQ(m.command(), "quit");
  EXPECT_EQ(m.nargs(), 1);
  EXPECT_EQ(m.arg(0), "some message here");
}

TEST(MessageTest, ParsePrefixedCommandAndArgs) {
  Message m;
  bool ok = m.Parse(":irc.server whois foo bar");
  ASSERT_TRUE(ok);
  EXPECT_EQ(m.prefix(), "irc.server");
  EXPECT_EQ(m.command(), "whois");
  EXPECT_EQ(m.nargs(), 2);
  EXPECT_EQ(m.arg(0), "foo");
  EXPECT_EQ(m.arg(1), "bar");
}

TEST(MessageTest, ParsePrefixedCommandAndArgsAndTrailing) {
  Message m;
  bool ok = m.Parse(":irc.server whois foo bar :extra stuff");
  ASSERT_TRUE(ok);
  EXPECT_EQ(m.prefix(), "irc.server");
  EXPECT_EQ(m.command(), "whois");
  EXPECT_EQ(m.nargs(), 3);
  EXPECT_EQ(m.arg(0), "foo");
  EXPECT_EQ(m.arg(1), "bar");
  EXPECT_EQ(m.arg(2), "extra stuff");
}

// Parsing of corner cases.

TEST(MessageTest, ParsePrefixOnly) {
  Message m;
  bool ok = m.Parse(":irc.server ");
  ASSERT_FALSE(ok);
}

TEST(MessageTest, ParseColonInside) {
  Message m;
  bool ok = m.Parse("what is:this :thing :about");
  ASSERT_TRUE(ok);
  EXPECT_TRUE(m.prefix().empty());
  EXPECT_EQ(m.command(), "what");
  EXPECT_EQ(m.nargs(), 2);
  EXPECT_EQ(m.arg(0), "is:this");
  EXPECT_EQ(m.arg(1), "thing :about");
}

TEST(MessageTest, ParseExtraSpaces) {
  Message m;
  bool ok = m.Parse(":foo     bar   baz\tquux    :  huh");
  ASSERT_TRUE(ok);
  EXPECT_EQ(m.prefix(), "foo");
  EXPECT_EQ(m.command(), "bar");
  EXPECT_EQ(m.nargs(), 2);
  EXPECT_EQ(m.arg(0), "baz\tquux");
  EXPECT_EQ(m.arg(1), "  huh");
}

// Parsing of the prefix nick!user@host parts.

TEST(MessageTest, ParsePrefixNick) {
  Message m;
  bool ok = m.Parse(":nick!user@host PRIVMSG :hey");
  ASSERT_TRUE(ok);
  EXPECT_EQ(m.prefix_nick(), "nick");
}

TEST(MessageTest, ParsePrefixNick_NoSep1) {
  Message m;
  bool ok = m.Parse(":something@host PRIVMSG :hey");
  ASSERT_TRUE(ok);
  EXPECT_TRUE(m.prefix_nick().empty());
}

TEST(MessageTest, ParsePrefixNick_NoSep2) {
  Message m;
  bool ok = m.Parse(":nick!something PRIVMSG :hey");
  ASSERT_TRUE(ok);
  EXPECT_TRUE(m.prefix_nick().empty());
}

TEST(MessageTest, ParsePrefixNick_EmptyUser) {
  Message m;
  bool ok = m.Parse(":nick!@host PRIVMSG :hey");
  ASSERT_TRUE(ok);
  EXPECT_TRUE(m.prefix_nick().empty());
}

TEST(MessageTest, ParsePrefixNick_EmptyHost) {
  Message m;
  bool ok = m.Parse(":nick!user@ PRIVMSG :hey");
  ASSERT_TRUE(ok);
  EXPECT_TRUE(m.prefix_nick().empty());
}

TEST(MessageTest, ParsePrefixNick_EmptyUserHost) {
  Message m;
  bool ok = m.Parse(":nick!@ PRIVMSG :hey");
  ASSERT_TRUE(ok);
  EXPECT_TRUE(m.prefix_nick().empty());
}

// Verify that we don't read past the given count.

TEST(MessageTest, ParseStopAtCount) {
  Message m;
  const unsigned char* data = reinterpret_cast<const unsigned char*>(":foo bar baz :quux");

  ASSERT_FALSE(m.Parse(data, 0));  // stop at start

  ASSERT_FALSE(m.Parse(data, 2));  // stop in prefix

  ASSERT_FALSE(m.Parse(data, 5));  // stop at start of command

  ASSERT_TRUE(m.Parse(data, 7));  // stop inside command
  EXPECT_EQ(m.prefix(), "foo");
  EXPECT_EQ(m.command(), "ba");
  EXPECT_EQ(m.nargs(), 0);

  ASSERT_TRUE(m.Parse(data, 9));  // stop before first argument
  EXPECT_EQ(m.prefix(), "foo");
  EXPECT_EQ(m.command(), "bar");
  EXPECT_EQ(m.nargs(), 0);

  ASSERT_TRUE(m.Parse(data, 11));  // stop inside regular argument
  EXPECT_EQ(m.prefix(), "foo");
  EXPECT_EQ(m.command(), "bar");
  EXPECT_EQ(m.nargs(), 1);
  EXPECT_EQ(m.arg(0), "ba");

  ASSERT_TRUE(m.Parse(data, 16));  // stop inside trailing argument
  EXPECT_EQ(m.prefix(), "foo");
  EXPECT_EQ(m.command(), "bar");
  EXPECT_EQ(m.nargs(), 2);
  EXPECT_EQ(m.arg(0), "baz");
  EXPECT_EQ(m.arg(1), "qu");
}

// Write simple formats.

TEST(MessageTest, WriteCommand) {
  Message m = { "quit" };
  unsigned char buf[4];
  std::size_t size = m.Write(buf, sizeof buf);

  ASSERT_EQ(size, 4u);
  EXPECT_TRUE(std::memcmp(buf, "quit", 4) == 0);
}

TEST(MessageTest, WriteCommandAndArgs) {
  Message m = { "whois", "foo", "bar" };
  unsigned char buf[13];
  std::size_t size = m.Write(buf, sizeof buf);

  ASSERT_EQ(size, 13u);
  EXPECT_TRUE(std::memcmp(buf, "whois foo bar", 13) == 0);
}

TEST(MessageTest, WriteCommandAndTrailing) {
  Message m = { "quit", "some message here" };
  unsigned char buf[23];
  std::size_t size = m.Write(buf, sizeof buf);

  ASSERT_EQ(size, 23u);
  EXPECT_TRUE(std::memcmp(buf, "quit :some message here", 23) == 0);
}

TEST(MessageTest, WriteCommandAndArgsAndTrailing) {
  Message m = { "whois", "foo", "bar", "extra stuff" };
  unsigned char buf[26];
  std::size_t size = m.Write(buf, sizeof buf);

  ASSERT_EQ(size, 26u);
  EXPECT_TRUE(std::memcmp(buf, "whois foo bar :extra stuff", 26) == 0);
}

TEST(MessageTest, WritePrefixedCommand) {
  Message m({ "quit" }, "irc.server");
  unsigned char buf[16];
  std::size_t size = m.Write(buf, sizeof buf);

  ASSERT_EQ(size, 16u);
  EXPECT_TRUE(std::memcmp(buf, ":irc.server quit", 16) == 0);
}

TEST(MessageTest, WritePrefixedCommandAndArgs) {
  Message m({ "whois", "foo", "bar" }, "irc.server");
  unsigned char buf[25];
  std::size_t size = m.Write(buf, sizeof buf);

  ASSERT_EQ(size, 25u);
  EXPECT_TRUE(std::memcmp(buf, ":irc.server whois foo bar", 25) == 0);
}

TEST(MessageTest, WritePrefixedCommandAndTrailing) {
  Message m({ "quit", "some message here" }, "irc.server");
  unsigned char buf[35];
  std::size_t size = m.Write(buf, sizeof buf);

  ASSERT_EQ(size, 35u);
  EXPECT_TRUE(std::memcmp(buf, ":irc.server quit :some message here", 35) == 0);
}

TEST(MessageTest, WritePrefixedCommandAndArgsAndTrailing) {
  Message m({ "whois", "foo", "bar", "extra stuff" }, "irc.server");
  unsigned char buf[38];
  std::size_t size = m.Write(buf, sizeof buf);

  ASSERT_EQ(size, 38u);
  EXPECT_TRUE(std::memcmp(buf, ":irc.server whois foo bar :extra stuff", 38) == 0);
}

// Verify that we don't write past the given count.

TEST(MessageTest, WriteStopAtCount) {
  Message m({ "bar", "baz", "quux zuul" }, "foo");
  unsigned char buf[24] = {0};
  char truth[24] = ":foo bar baz :quux zuul";

  EXPECT_EQ(m.Write(nullptr, 0), 23u);

  for (std::size_t count = 0; count <= 23; ++count) {
    EXPECT_EQ(m.Write(buf, count), 23u);
    EXPECT_TRUE(std::memcmp(buf, truth, count) == 0);
    EXPECT_EQ(buf[count], '\0');
  }
}

} // namespace irc
