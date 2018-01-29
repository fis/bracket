/** \file
 * Byte buffers and ring buffers.
 */

#ifndef BASE_BUFFER_H_
#define BASE_BUFFER_H_

#include <array>
#include <cstdlib>
#include <vector>

#include "base/log.h"

namespace base {

/** Fixed-size array of bytes. */
template <std::size_t N>
using byte_array = std::array<unsigned char, N>;

/** Resizable buffer of bytes. */
using byte_buffer = std::vector<unsigned char>;

/** View into an existing buffer. Holds a pointer and a size. */
class byte_view {
 public:
  /** Makes a new, invalid view. */
  byte_view() : data_(nullptr), size_(0) {}
  /** Makes a new view from the provided parameters. */
  byte_view(unsigned char* d, std::size_t s) : data_(d), size_(s) {}

  /** Returns `true` if the view is pointing at valid data. */
  bool valid() const noexcept { return data_ != nullptr; }

  unsigned char* data() noexcept { return data_; }
  const unsigned char* data() const noexcept { return data_; }

  std::size_t size() const noexcept { return size_; }

  unsigned char& operator[](std::size_t i) noexcept { return data_[i]; }
  const unsigned char& operator[](std::size_t i) const noexcept { return data_[i]; }
  // TODO: at?

  unsigned char* begin() noexcept { return data_; }
  const unsigned char* begin() const noexcept { return data_; }
  const unsigned char* cbegin() const noexcept { return data_; }

  unsigned char* end() noexcept { return data_ + size_; }
  const unsigned char* end() const noexcept { return data_ + size_; }
  const unsigned char* cend() const noexcept { return data_ + size_; }

 private:
  /** Pointer to the data of the view. */
  unsigned char* data_;
  /** Size of the pointed-to data. */
  std::size_t size_;
};

/** Automatically resizable ring buffer, for FIFO queues of bytes. */
class ring_buffer {
 public:
  /** Constructs a new ring buffer. \p initial_size must be a power of 2. */
  ring_buffer(std::size_t initial_size = 4096)
      : data_(new unsigned char[initial_size]), size_(initial_size) {
    CHECK(!(initial_size & (initial_size - 1)));
  }

  /** Destroys this buffer, freeing the memory. */
  ~ring_buffer() {
    delete[] data_;
  }

  /**
   * Allocates space for \p push_size bytes in the queue.
   *
   * The caller is responsible for writing some data. Of the two byte_view objects that are
   * returned, the first one is always valid, and has a size between 1 and \p push_size. The second
   * byte_view is only valid if the first one is smaller than the requested size, in which case
   * *its* size will be `push_size - first.size`. This happens when the requested range crosses the
   * wrap-around point of the buffer.
   */
  std::pair<byte_view, byte_view> push(std::size_t push_size);

  /** Allocates and writes a single byte. */
  void push_byte(unsigned char c) {
    if (used_ == size_) {
      push(1).first[0] = c;
    } else {
      std::size_t end = (first_byte_ + used_) & (size_ - 1);
      data_[end] = c;
      ++used_;
    }
  }

  /**
   * Deallocates storage from the end of the buffer.
   *
   * This method can be useful if you need to push() something for which you know an upper bound,
   * but not the exact size. Call push() with the upper bound size, write your contents, and then
   * unpush() the remaining space.
   *
   * Be aware that the push() call will ensure there is sufficient free space for the entire length,
   * potentially allocating memory. Only use this approach if the upper bound is relatively tight.
   */
  void unpush(std::size_t size) {
    CHECK(size <= used_);
    used_ -= size;
    if (!used_)
      first_byte_ = 0;
  }
  /**
   * Returns a view to the first \p size bytes of the queue.
   *
   * The argument must be at most #size(). As with push(), the first returned view is always valid,
   * and the second one only if the boundary is crossed. `first.size + second.size` will be equal to
   * the requested size.
   */
  std::pair<byte_view, byte_view> front(std::size_t size) {
    CHECK(size <= used_);
    return ViewFrom(first_byte_, size);
  }

  /** Deallocates first \p pop_size bytes. Must be at most #size(). */
  void pop(std::size_t pop_size);

  /** Resets the queue to empty. */
  void clear() noexcept {
    used_ = 0;
    first_byte_ = 0;
  }

  /** Returns the number of bytes stored in the queue. */
  std::size_t size() const noexcept { return used_; }
  /** Returns the amount of memory allocated for the queue. */
  std::size_t capacity() const noexcept { return size_; }

 private:
  /** Constructs a view for a region. */
  std::pair<byte_view, byte_view> ViewFrom(std::size_t start, std::size_t view_size) {
    if (start + view_size <= size_) {
      // no wrapping
      return std::make_pair(byte_view(data_ + start, view_size), byte_view());
    } else {
      // two pieces
      std::size_t first_piece = size_ - start;
      return std::make_pair(
          byte_view(data_ + start, first_piece),
          byte_view(data_, view_size - first_piece));
    }
  }

  /** Allocated data. */
  unsigned char *data_;
  /** Size of allocated data. */
  std::size_t size_;
  /** Number of bytes in the queue. */
  std::size_t used_ = 0;
  /** Offset of the first (earliest inserted) byte. */
  std::size_t first_byte_ = 0;
};

template <typename Buffer>
std::int8_t read_i8(const Buffer& b, std::size_t i = 0) { return (std::int8_t) b[i]; }

template <typename Buffer>
std::uint8_t read_u8(const Buffer& b, std::size_t i = 0) { return b[i]; }

template <typename Buffer>
std::int16_t read_i16(const Buffer& b, std::size_t i = 0) { return (std::int16_t) b[i] | (std::int16_t) b[i+1] << 8; }

template <typename Buffer>
std::int16_t read_u16(const Buffer& b, std::size_t i = 0) { return (std::uint16_t) b[i] | (std::uint16_t) b[i+1] << 8; }

template <typename Buffer>
std::int32_t read_i32(const Buffer& b, std::size_t i = 0) {
  return (std::int32_t) b[i] | (std::int32_t) b[i+1] << 8 | (std::int32_t) b[i+2] << 16 | (std::int32_t) b[i+3] << 24;
}

template <typename Buffer>
std::uint32_t read_u32(const Buffer& b, std::size_t i = 0) {
  return (std::uint32_t) b[i] | (std::uint32_t) b[i+1] << 8 | (std::uint32_t) b[i+2] << 16 | (std::uint32_t) b[i+3] << 24;
}

template <typename Buffer>
void write_i8(std::int8_t v, Buffer& b, std::size_t i = 0) { b[i] = v; }

template <typename Buffer>
void write_u8(std::uint8_t v, Buffer& b, std::size_t i = 0) { b[i] = v; }

template <typename Buffer>
void write_i16(std::int16_t v, Buffer& b, std::size_t i = 0) { b[i] = v; b[i+1] = v >> 8; }

template <typename Buffer>
void write_u16(std::uint16_t v, Buffer& b, std::size_t i = 0) { b[i] = v; b[i+1] = v >> 8; }

template <typename Buffer>
void write_i32(std::int32_t v, Buffer& b, std::size_t i = 0) {
  b[i] = v; b[i+1] = v >> 8; b[i+2] = v >> 16; b[i+3] = v >> 24;
}

template <typename Buffer>
void write_u32(std::uint32_t v, Buffer& b, std::size_t i = 0) {
  b[i] = v; b[i+1] = v >> 8; b[i+2] = v >> 16; b[i+3] = v >> 24;
}

} // namespace base

#endif // BASE_BUFFER_H_

// Local Variables:
// mode: c++
// End:
