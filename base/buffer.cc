#include <cstring>

#include "base/buffer.h"

namespace base {

// Byte buffer utilities.
// ======================

std::pair<byte_view, byte_view> ring_buffer::push(std::size_t push_size) {
  if (used_ + push_size > size_) {
    // need more space
    std::size_t new_size = size_ << 1;
    while (new_size && used_ + push_size > new_size)
      new_size <<= 1;
    CHECK(new_size > 0);
    resize(new_size);
  }

  std::size_t end = (first_byte_ + used_) & (size_ - 1);
  used_ += push_size;

  return view_from(end, push_size);
}

byte* ring_buffer::push_cont(std::size_t push_size) {
  if (used_ + push_size > size_) {
    // always contiguous after a resize
    auto pushed = push(push_size);
    CHECK(!pushed.second.valid());
    return pushed.first.data();
  }

  std::size_t end = (first_byte_ + used_) & (size_ - 1);
  if (end + push_size <= size_) {
    // easy case, no wraparound
    used_ += push_size;
    return data_ + end;
  } else {
    // would wrap around, need to move [__abc__] -> [abc____]
    std::memmove(data_, data_ + first_byte_, used_);
    first_byte_ = 0;
    end = used_;
    used_ += push_size;
    return data_ + end;
  }
}

byte_view ring_buffer::push_free() {
  if (used_ == size_) {
    std::size_t new_size = size_ << 1;
    CHECK(new_size > 0);
    resize(new_size);
  }

  std::size_t end = (first_byte_ + used_) & (size_ - 1);
  std::size_t free = size_ - used_;
  if (end + free > size_)
    free = size_ - end;

  used_ += free;
  return byte_view(data_ + end, free);
}

void ring_buffer::resize(std::size_t new_size) {
  byte* new_data = new byte[new_size];

  if (first_byte_ + used_ <= size_) {
    std::memcpy(new_data, data_ + first_byte_, used_);
  } else {
    std::size_t first_half = size_ - first_byte_;
    std::memcpy(new_data, data_ + first_byte_, first_half);
    std::memcpy(new_data + first_half, data_, used_ - first_half);
  }

  delete[] data_;
  data_ = new_data;
  size_ = new_size;
  first_byte_ = 0;
}

#if 0  // currently not needed
void ring_buffer::normalize() {
  // [abcd____]
  if (first_byte_ == 0)
    return;
  // [__abcd__]
  if (first_byte_ + used_ > size_) {
    std::memmove(data_, data_ + first_byte_, used_);
    first_byte_ = 0;
    return;
  }
  // [cd____ab] (with free space to move 'cd' to its final position)
  std::size_t head = size_ - first_byte_;
  std::size_t tail = (first_byte_ + used_) & (size_ - 1);
  if (used_ <= first_byte_) {
    std::memmove(data_ + head, data_, tail);
    std::memmove(data_, data_ + first_byte_, head);
    first_byte_ = 0;
    return;
  }
  // [cdefg_ab] (just swap to a new array)
  byte* new_data = new byte[size_];
  std::memcpy(new_data, data_ + first_byte_, head);
  std::memcpy(new_data + head, data_, tail);
  delete[] data_;
  data_ = new_data;
  first_byte_ = 0;
}
#endif

} // namespace base
