#include <cstring>

#include "base/buffer.h"
#include "gtest/gtest.h"

namespace base {

TEST(ByteViewTest, AccessBytes) {
  byte data[6] = {1, 2, 3, 4, 5, 6};
  byte_view view{data+2, 2};

  EXPECT_EQ(view[0], 3);
  EXPECT_EQ(view[1], 4);

  view[0] = 7;
  view[1] = 8;

  EXPECT_EQ(data[0], 1); EXPECT_EQ(data[1], 2);
  EXPECT_EQ(data[2], 7); EXPECT_EQ(data[3], 8);
  EXPECT_EQ(data[4], 5); EXPECT_EQ(data[5], 6);
}

TEST(RingBufferTest, PushWrapAround) {
  ring_buffer buffer(16);
  auto* base = buffer.push(1).first.data();
  buffer.clear();

  auto d = buffer.push(14);

  EXPECT_EQ(buffer.size(), 14u);
  EXPECT_EQ(buffer.free_cont(), 2u);
  EXPECT_TRUE(d.first.valid());
  EXPECT_EQ(d.first.data(), base);
  EXPECT_EQ(d.first.size(), 14u);
  EXPECT_FALSE(d.second.valid());

  buffer.pop(12);

  EXPECT_EQ(buffer.size(), 2u);
  EXPECT_EQ(buffer.free_cont(), 2u);

  auto d2 = buffer.push(8);

  EXPECT_EQ(buffer.size(), 10u);
  EXPECT_EQ(buffer.free_cont(), 6u);
  EXPECT_TRUE(d2.first.valid());
  EXPECT_EQ(d2.first.data(), base + 14);
  EXPECT_EQ(d2.first.size(), 2u);
  EXPECT_TRUE(d2.second.valid());
  EXPECT_EQ(d2.second.data(), base);
  EXPECT_EQ(d2.second.size(), 6u);
}

TEST(RingBufferTest, PushContiguous) {
  ring_buffer buffer(16);
  auto* base = buffer.push(14).first.data();
  std::memcpy(base, "abcdefghijklmn", 14);
  buffer.pop(12);
  auto* d = buffer.push_cont(8);
  std::memcpy(d, "opqrstuv", 8);

  EXPECT_EQ(buffer.size(), 10u);
  EXPECT_EQ(d, base + 2);
  EXPECT_EQ(std::memcmp(base, "mnopqrstuv", 10), 0);
}

TEST(RingBufferTest, PushFree) {
  ring_buffer buffer(16);
  auto* base = buffer.push(14).first.data();
  buffer.pop(8);
  EXPECT_EQ(buffer.size(), 6u);

  auto tail = buffer.push_free();
  EXPECT_EQ(buffer.size(), 8u);
  EXPECT_EQ(tail.data(), base + 14);
  EXPECT_EQ(tail.size(), 2u);

  auto head = buffer.push_free();
  EXPECT_EQ(buffer.size(), 16u);
  EXPECT_EQ(head.data(), base);
  EXPECT_EQ(head.size(), 8u);

  auto resized = buffer.push_free();
  EXPECT_EQ(buffer.size(), 32u);
  EXPECT_EQ(resized.data(), &buffer[0] + 16);
  EXPECT_EQ(resized.size(), 16u);
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

TEST(RingBufferTest, ReadWritePrimitives) {
  ring_buffer buffer(4);
  buffer.write_i8(0x01);
  buffer.write_u8(0x81);
  buffer.write_i16(0x0203);
  buffer.write_i16(0x8283);
  buffer.write_i32(0x04050607);
  buffer.write_u32(0x84858687);
  EXPECT_EQ(buffer.read_u32(), 0x02038101);
  EXPECT_EQ(buffer.read_i32(), 0x06078283);
  EXPECT_EQ(buffer.read_u16(), 0x0405);
  EXPECT_EQ(buffer.read_i16(), (std::int16_t)0x8687);
  EXPECT_EQ(buffer.read_u8(), 0x85);
  EXPECT_EQ(buffer.read_i8(), (std::int8_t)0x84);
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

TEST(RingBufferTest, PushCharResize) {
  ring_buffer buffer(4);

  buffer.push(2);
  buffer.write_u8('a');
  buffer.write_u8('b');
  buffer.pop(2);
  buffer.write_u8('c');
  buffer.write_u8('d');

  auto d = buffer.front(4);

  EXPECT_EQ(buffer.size(), 4u);
  EXPECT_TRUE(d.first.valid());
  EXPECT_TRUE(d.second.valid());
  EXPECT_EQ(d.first.size(), 2u);
  EXPECT_EQ(d.second.size(), 2u);
  EXPECT_EQ(d.first.data(), d.second.data() + 2);
  EXPECT_TRUE(std::memcmp(d.second.data(), "cdab", 4) == 0);

  buffer.write_u8('e');
  auto d2 = buffer.front(5);

  EXPECT_EQ(buffer.size(), 5u);
  EXPECT_EQ(buffer.capacity(), 8u);
  EXPECT_TRUE(d2.first.valid());
  EXPECT_EQ(d2.first.size(), 5u);
  EXPECT_TRUE(std::memcmp(d2.first.data(), "abcde", 5) == 0);
  EXPECT_FALSE(d2.second.valid());
}

} // namespace base
