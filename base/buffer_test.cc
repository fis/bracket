#include <cstring>

#include "base/buffer.h"
#include "gtest/gtest.h"

namespace base {

TEST(RingBufferTest, PushWrapAround) {
  ring_buffer buffer(16);
  auto* base = buffer.push(1).first.data();
  buffer.clear();

  auto d = buffer.push(14);

  EXPECT_EQ(buffer.size(), 14u);
  EXPECT_TRUE(d.first.valid());
  EXPECT_EQ(d.first.data(), base);
  EXPECT_EQ(d.first.size(), 14u);
  EXPECT_FALSE(d.second.valid());

  buffer.pop(12);

  EXPECT_EQ(buffer.size(), 2u);

  auto d2 = buffer.push(8);

  EXPECT_EQ(buffer.size(), 10u);
  EXPECT_TRUE(d2.first.valid());
  EXPECT_EQ(d2.first.data(), base + 14);
  EXPECT_EQ(d2.first.size(), 2u);
  EXPECT_TRUE(d2.second.valid());
  EXPECT_EQ(d2.second.data(), base);
  EXPECT_EQ(d2.second.size(), 6u);
}

TEST(RingBufferTest, Unpush) {
  ring_buffer buffer(16);
  auto* base = buffer.push(1).first.data();
  buffer.clear();

  buffer.push(14);
  buffer.pop(6);
  buffer.unpush(2);
  auto d = buffer.push(6);

  EXPECT_EQ(buffer.size(), 12u);
  EXPECT_TRUE(d.first.valid());
  EXPECT_EQ(d.first.data(), base + 12);
  EXPECT_EQ(d.first.size(), 4u);
  EXPECT_TRUE(d.second.valid());
  EXPECT_EQ(d.second.data(), base);
  EXPECT_EQ(d.second.size(), 2u);

  buffer.unpush(4);
  auto d2 = buffer.push(1);

  EXPECT_EQ(buffer.size(), 9u);
  EXPECT_TRUE(d2.first.valid());
  EXPECT_EQ(d2.first.data(), base + 14);
  EXPECT_EQ(d2.first.size(), 1u);
  EXPECT_FALSE(d2.second.valid());
}

TEST(RingBufferTest, Full) {
  ring_buffer buffer(16);
  auto* base = buffer.push(1).first.data();
  buffer.clear();

  auto d = buffer.push(16);

  EXPECT_EQ(buffer.size(), 16u);
  EXPECT_TRUE(d.first.valid());
  EXPECT_EQ(d.first.data(), base);
  EXPECT_EQ(d.first.size(), 16u);
  EXPECT_FALSE(d.second.valid());

  buffer.pop(8);
  buffer.push(8);
  buffer.pop(15);
  auto d2 = buffer.push(15);

  EXPECT_EQ(buffer.size(), 16u);
  EXPECT_TRUE(d2.first.valid());
  EXPECT_EQ(d2.first.data(), base + 8);
  EXPECT_EQ(d2.first.size(), 8u);
  EXPECT_TRUE(d2.second.valid());
  EXPECT_EQ(d2.second.data(), base);
  EXPECT_EQ(d2.second.size(), 7u);
}

TEST(RingBufferTest, Resize) {
  ring_buffer buffer(8);

  buffer.push(4);
  buffer.push(60);

  auto d = buffer.front(64);

  EXPECT_EQ(buffer.size(), 64u);
  EXPECT_EQ(buffer.capacity(), 64u);
  EXPECT_TRUE(d.first.valid());
  EXPECT_EQ(d.first.size(), 64u);
  EXPECT_FALSE(d.second.valid());
}

TEST(RingBufferTest, PushChar) {
  ring_buffer buffer(4);

  buffer.push(2);
  buffer.push_byte('a');
  buffer.push_byte('b');
  buffer.pop(2);
  buffer.push_byte('c');
  buffer.push_byte('d');

  auto d = buffer.front(4);

  EXPECT_EQ(buffer.size(), 4u);
  EXPECT_TRUE(d.first.valid());
  EXPECT_TRUE(d.second.valid());
  EXPECT_EQ(d.first.size(), 2u);
  EXPECT_EQ(d.second.size(), 2u);
  EXPECT_EQ(d.first.data(), d.second.data() + 2);
  EXPECT_TRUE(std::memcmp(d.second.data(), "cdab", 4) == 0);

  buffer.push_byte('e');
  auto d2 = buffer.front(5);

  EXPECT_EQ(buffer.size(), 5u);
  EXPECT_EQ(buffer.capacity(), 8u);
  EXPECT_TRUE(d2.first.valid());
  EXPECT_EQ(d2.first.size(), 5u);
  EXPECT_TRUE(std::memcmp(d2.first.data(), "abcde", 5) == 0);
  EXPECT_FALSE(d2.second.valid());
}

} // namespace base
