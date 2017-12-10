#include <cstring>

#include "base/buffer.h"

namespace base {

// Byte buffer utilities.
// ======================

std::pair<BufferView, BufferView> RingBuffer::Push(BufferSize push_size) {
  if (used_ + push_size > size_) {
    // need more space

    BufferSize new_size = size_ << 1;
    while (new_size && used_ + push_size > new_size)
      new_size <<= 1;
    CHECK(new_size > 0);

    char* new_data = new char[new_size];

    if (first_byte_ + used_ <= size_) {
      std::memcpy(new_data, data_ + first_byte_, used_);
    } else {
      BufferSize first_half = size_ - first_byte_;
      std::memcpy(new_data, data_ + first_byte_, first_half);
      std::memcpy(new_data + first_half, data_, used_ - first_half);
    }

    delete[] data_;
    data_ = new_data;
    size_ = new_size;
    first_byte_ = 0;
  }

  BufferSize end = (first_byte_ + used_) & (size_ - 1);
  used_ += push_size;

  return ViewFrom(end, push_size);
}

void RingBuffer::Pop(BufferSize pop_size) {
  /// \todo trimming

  CHECK(pop_size <= used_);

  used_ -= pop_size;
  if (!used_)
    first_byte_ = 0;
  else
    first_byte_ = (first_byte_ + pop_size) & (size_ - 1);
}

} // namespace base
