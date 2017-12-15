#include <cstring>

#include "base/buffer.h"
#include "gtest/gtest.h"

namespace base {

TEST(RingBufferTest, PushWrapAround) {
  RingBuffer buffer(16);
  char* base = buffer.Push(1).first.data;
  buffer.Clear();

  auto d = buffer.Push(14);

  EXPECT_EQ(buffer.size(), 14u);
  EXPECT_TRUE(d.first.valid());
  EXPECT_EQ(d.first.data, base);
  EXPECT_EQ(d.first.size, 14u);
  EXPECT_FALSE(d.second.valid());

  buffer.Pop(12);

  EXPECT_EQ(buffer.size(), 2u);

  auto d2 = buffer.Push(8);

  EXPECT_EQ(buffer.size(), 10u);
  EXPECT_TRUE(d2.first.valid());
  EXPECT_EQ(d2.first.data, base + 14);
  EXPECT_EQ(d2.first.size, 2u);
  EXPECT_TRUE(d2.second.valid());
  EXPECT_EQ(d2.second.data, base);
  EXPECT_EQ(d2.second.size, 6u);
}

TEST(RingBufferTest, Unpush) {
  RingBuffer buffer(16);
  char* base = buffer.Push(1).first.data;
  buffer.Clear();

  buffer.Push(14);
  buffer.Pop(6);
  buffer.Unpush(2);
  auto d = buffer.Push(6);

  EXPECT_EQ(buffer.size(), 12u);
  EXPECT_TRUE(d.first.valid());
  EXPECT_EQ(d.first.data, base + 12);
  EXPECT_EQ(d.first.size, 4u);
  EXPECT_TRUE(d.second.valid());
  EXPECT_EQ(d.second.data, base);
  EXPECT_EQ(d.second.size, 2u);

  buffer.Unpush(4);
  auto d2 = buffer.Push(1);

  EXPECT_EQ(buffer.size(), 9u);
  EXPECT_TRUE(d2.first.valid());
  EXPECT_EQ(d2.first.data, base + 14);
  EXPECT_EQ(d2.first.size, 1u);
  EXPECT_FALSE(d2.second.valid());
}

TEST(RingBufferTest, Full) {
  RingBuffer buffer(16);
  char* base = buffer.Push(1).first.data;
  buffer.Clear();

  auto d = buffer.Push(16);

  EXPECT_EQ(buffer.size(), 16u);
  EXPECT_TRUE(d.first.valid());
  EXPECT_EQ(d.first.data, base);
  EXPECT_EQ(d.first.size, 16u);
  EXPECT_FALSE(d.second.valid());

  buffer.Pop(8);
  buffer.Push(8);
  buffer.Pop(15);
  auto d2 = buffer.Push(15);

  EXPECT_EQ(buffer.size(), 16u);
  EXPECT_TRUE(d2.first.valid());
  EXPECT_EQ(d2.first.data, base + 8);
  EXPECT_EQ(d2.first.size, 8u);
  EXPECT_TRUE(d2.second.valid());
  EXPECT_EQ(d2.second.data, base);
  EXPECT_EQ(d2.second.size, 7u);
}

TEST(RingBufferTest, Resize) {
  RingBuffer buffer(8);

  buffer.Push(4);
  buffer.Push(60);

  auto d = buffer.Front(64);

  EXPECT_EQ(buffer.size(), 64u);
  EXPECT_EQ(buffer.capacity(), 64u);
  EXPECT_TRUE(d.first.valid());
  EXPECT_EQ(d.first.size, 64u);
  EXPECT_FALSE(d.second.valid());
}

TEST(RingBufferTest, PushChar) {
  RingBuffer buffer(4);

  buffer.Push(2);
  buffer.PushChar('a');
  buffer.PushChar('b');
  buffer.Pop(2);
  buffer.PushChar('c');
  buffer.PushChar('d');

  auto d = buffer.Front(4);

  EXPECT_EQ(buffer.size(), 4u);
  EXPECT_TRUE(d.first.valid());
  EXPECT_TRUE(d.second.valid());
  EXPECT_EQ(d.first.size, 2u);
  EXPECT_EQ(d.second.size, 2u);
  EXPECT_EQ(d.first.data, d.second.data + 2);
  EXPECT_TRUE(std::memcmp(d.second.data, "cdab", 4) == 0);

  buffer.PushChar('e');
  auto d2 = buffer.Front(5);

  EXPECT_EQ(buffer.size(), 5u);
  EXPECT_EQ(buffer.capacity(), 8u);
  EXPECT_TRUE(d2.first.valid());
  EXPECT_EQ(d2.first.size, 5u);
  EXPECT_TRUE(std::memcmp(d2.first.data, "abcde", 5) == 0);
  EXPECT_FALSE(d2.second.valid());
}

} // namespace base
