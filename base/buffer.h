/** \file
 * Byte buffers and ring buffers.
 */

#ifndef BASE_BUFFER_H_
#define BASE_BUFFER_H_

#include <array>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "base/log.h"

namespace base {

/** Short name for unsigned char. */
using byte = unsigned char;

/** Reads a signed 8-bit integer from binary data. */
inline std::int8_t read_i8(const byte* b) { return (std::int8_t) b[0]; }
/** Reads an unsigned 8-bit integer from binary data. */
inline std::uint8_t read_u8(const byte* b) { return b[0]; }
/** Reads a signed 16-bit integer from binary data. */
inline std::int16_t read_i16(const byte* b) { return (std::int16_t) b[0] | (std::int16_t) b[1] << 8; }
/** Reads an unsigned 16-bit integer from binary data. */
inline std::int16_t read_u16(const byte* b) { return (std::uint16_t) b[0] | (std::uint16_t) b[1] << 8; }
/** Reads a signed 32-bit integer from binary data. */
inline std::int32_t read_i32(const byte* b) {
  return (std::int32_t) b[0] | (std::int32_t) b[1] << 8 | (std::int32_t) b[2] << 16 | (std::int32_t) b[3] << 24;
}
/** Reads an unsigned 32-bit integer from binary data. */
inline std::uint32_t read_u32(const byte* b) {
  return (std::uint32_t) b[0] | (std::uint32_t) b[1] << 8 | (std::uint32_t) b[2] << 16 | (std::uint32_t) b[3] << 24;
}

/** Writes a signed 8-bit integer into binary data. */
inline void write_i8(std::int8_t v, byte* b) { b[0] = v; }
/** Writes an unsigned 8-bit integer into binary data. */
inline void write_u8(std::uint8_t v, byte* b) { b[0] = v; }
/** Writes a signed 16-bit integer into binary data. */
inline void write_i16(std::int16_t v, byte* b) { b[0] = v; b[1] = v >> 8; }
/** Writes an unsigned 16-bit integer into binary data. */
inline void write_u16(std::uint16_t v, byte* b) { b[0] = v; b[1] = v >> 8; }
/** Writes a signed 32-bit integer into binary data. */
inline void write_i32(std::int32_t v, byte* b) {
  b[0] = v; b[1] = v >> 8; b[2] = v >> 16; b[3] = v >> 24;
}
/** Writes an unsigned 32-bit integer into binary data. */
inline void write_u32(std::uint32_t v, byte* b) {
  b[0] = v; b[1] = v >> 8; b[2] = v >> 16; b[3] = v >> 24;
}

/** Fixed-size array of bytes. */
template <std::size_t N>
using byte_array = std::array<byte, N>;

/** Resizable buffer of bytes. */
using byte_buffer = std::vector<byte>;

/** View into an existing buffer. Holds a pointer and a size. */
class byte_view {
 public:
  /** Makes a new, invalid view. */
  byte_view() : data_(nullptr), size_(0) {}
  /** Makes a new view from the provided parameters. */
  byte_view(byte* d, std::size_t s) : data_(d), size_(s) {}

  /** Returns `true` if the view is pointing at valid data. */
  bool valid() const noexcept { return data_ != nullptr; }
  /** Conversion to bool, same result as valid(). */
  explicit operator bool() const noexcept { return valid(); }

  /** Returns the data pointer this view points at, or `nullptr` if not valid. */
  byte* data() noexcept { return data_; }
  /** \overload */
  const byte* data() const noexcept { return data_; }

  /** Returns the size of the view in bytes. */
  std::size_t size() const noexcept { return size_; }

  /** Accesses the specified byte of the view. */
  byte& operator[](std::size_t i) noexcept { return data_[i]; }
  /** \overload */
  const byte& operator[](std::size_t i) const noexcept { return data_[i]; }

  /** Returns a pointer to the beginning of the view. */
  byte* begin() noexcept { return data_; }
  /** \overload */
  const byte* begin() const noexcept { return data_; }
  /** Returns a const pointer to the beginning of the view. */
  const byte* cbegin() const noexcept { return data_; }

  /** Returns a pointer to one past the last byte of the view. */
  byte* end() noexcept { return data_ + size_; }
  /** \overload */
  const byte* end() const noexcept { return data_ + size_; }
  /** Returns a const pointer to one past the last byte of the view. */
  const byte* cend() const noexcept { return data_ + size_; }

 private:
  /** Pointer to the data of the view. */
  byte* data_;
  /** Size of the pointed-to data. */
  std::size_t size_;
};

/**
 * Automatically resizable ring buffer, for FIFO queues of bytes.
 *
 * This class does not fully abstract away dealing with the wrap-around, but merely makes it more
 * convenient. The primary read/write functions (front() and push()) may return two byte_view
 * objects when the corresponding region needs to wrap.
 */
class ring_buffer {
 public:
  /** Constructs a new ring buffer. \p initial_size must be a power of 2. */
  ring_buffer(std::size_t initial_size = 4096)
      : data_(new byte[initial_size]), size_(initial_size) {
    CHECK(!(initial_size & (initial_size - 1)));
  }

  /** Destroys this buffer, freeing the memory. */
  ~ring_buffer() {
    delete[] data_;
  }

  /**
   * Allocates space for \p push_size bytes in the queue.
   *
   * The caller is responsible for writing data to the newly reserved bytes. Of the two byte_view
   * objects that are returned, the first one is always valid, and has a size between 1 and \p
   * push_size. The second byte_view is only valid if the first one is smaller than the requested
   * size, in which case *its* size will be `push_size - first.size`. This happens when the
   * requested range crosses the wrap-around point of the buffer.
   */
  std::pair<byte_view, byte_view> push(std::size_t push_size);

  /**
   * Allocates \p push_size contiguous bytes in the queue.
   *
   * This function behaves otherwise like push(), except that the returned region is guaranteed to
   * be contiguous, and therefore only the pointer to the beginning of the region is returned. If
   * the requested range would cross the wrap-around point, the buffer contents are normalized so
   * that the used bytes are in the beginning of the array.
   */
  byte* push_cont(std::size_t push_size);

  /**
   * Allocates the largest possible contiguous chunk of memory, without resizing unless necessary.
   *
   * This returns the region from the end of the currently reserved area to the wrap-around point or
   * to the beginning of the reserved area, whichever comes first. If the buffer is full, its size
   * is doubled and the returned view will be the (now empty) second half.
   *
   * This method is intended to be used if you're streaming in an unspecified amount of
   * data. Calling #push_free() repeatedly will give you contiguous chunks to put the data in. When
   * the stream runs out, #unpush() the amount you did not write. If you know in advance how much
   * data is coming, calling #push() and writing to the returned view(s) can save in resize
   * requests.
   */
  byte_view push_free();

  /** Allocates \p size bytes and copies data from \p src there. */
  void write(const byte* src, std::size_t size) {
    std::size_t end = (first_byte_ + used_) & (size_ - 1);
    if (used_ + size <= size_ && end + size <= size_) {
      std::memcpy(data_ + end, src, size);
      used_ += size;
    } else {
      auto [head, tail] = push(size);
      std::memcpy(head.data(), src, head.size());
      if (tail.valid())
        std::memcpy(tail.data(), src + head.size(), tail.size());
    }
  }

  /** Allocates and writes a signed 8-bit integer. */
  void write_i8(std::int8_t v) { write(reinterpret_cast<const byte*>(&v), 1); }
  /** Allocates and writes an unsigned 8-bit integer. */
  void write_u8(std::uint8_t v) { write(&v, 1); }
  /** Allocates and writes a signed 16-bit integer. */
  void write_i16(std::int16_t v) { byte b[2]; base::write_i16(v, b); write(b, 2); }
  /** Allocates and writes an unsigned 16-bit integer. */
  void write_u16(std::uint16_t v) { byte b[2]; base::write_u16(v, b); write(b, 2); }
  /** Allocates and writes a signed 32-bit integer. */
  void write_i32(std::int32_t v) { byte b[4]; base::write_i32(v, b); write(b, 4); }
  /** Allocates and writes an unsigned 32-bit integer. */
  void write_u32(std::uint32_t v) { byte b[4]; base::write_u32(v, b); write(b, 4); }

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
   * The argument must be at most #size(). As with #push(), the first returned view is always valid,
   * and the second one only if the boundary is crossed. `first.size + second.size` will be equal to
   * the requested size.
   */
  std::pair<byte_view, byte_view> front(std::size_t size) {
    CHECK(size <= used_);
    return view_from(first_byte_, size);
  }

  /**
   * Returns a view to the next contiguous used region, or an invalid view if empty.
   *
   * This method is intended to be used if you want to stream all the data out of the queue. Call
   * #next(), consume the data, and then call #pop() with the returned size. Repeating the operation
   * twice is guaranteed to return all data out of the buffer.
   */
  byte_view next() {
    if (empty())
      return byte_view();
    else if (first_byte_ + used_ > size_)
      return byte_view(data_ + first_byte_, size_ - first_byte_);
    else
      return byte_view(data_ + first_byte_, used_);
  }

  /** Deallocates first \p size bytes. Must be at most #size(). */
  void pop(std::size_t size) {
    CHECK(size <= used_);
    used_ -= size;
    if (!used_)
      first_byte_ = 0;
    else
      first_byte_ = (first_byte_ + size) & (size_ - 1);
  }

  /** Deallocates \p size bytes, and copies their former contents to \p dst. */
  void read(byte* dst, std::size_t size) {
    CHECK(size <= used_);
    if (first_byte_ + size <= size_) {
      std::memcpy(dst, data_ + first_byte_, size);
    } else {
      auto [head, tail] = front(size);
      std::memcpy(dst, head.data(), head.size());
      if (tail.valid())
        std::memcpy(dst + head.size(), tail.data(), tail.size());
    }
    pop(size);
  }

  /** Deallocates and returns a signed 8-bit integer. */
  std::int8_t read_i8() { byte b[1]; read(b, 1); return base::read_i8(b); }
  /** Deallocates and returns an unsigned 8-bit integer. */
  std::uint8_t read_u8() { byte b[1]; read(b, 1); return base::read_u8(b); }
  /** Deallocates and returns a signed 16-bit integer. */
  std::int16_t read_i16() { byte b[2]; read(b, 2); return base::read_i16(b); }
  /** Deallocates and returns an unsigned 16-bit integer. */
  std::uint16_t read_u16() { byte b[2]; read(b, 2); return base::read_u16(b); }
  /** Deallocates and returns a signed 32-bit integer. */
  std::int32_t read_i32() { byte b[4]; read(b, 4); return base::read_i32(b); }
  /** Deallocates and returns an unsigned 32-bit integer. */
  std::uint32_t read_u32() { byte b[4]; read(b, 4); return base::read_u32(b); }

  /** Resets the queue to empty. */
  void clear() noexcept {
    used_ = 0;
    first_byte_ = 0;
  }

  /** Returns `true` if the buffer is empty. */
  bool empty() const noexcept { return size() == 0; }
  /** Returns the number of bytes stored in the queue. */
  std::size_t size() const noexcept { return used_; }
  /** Returns the amount of memory allocated for the queue. */
  std::size_t capacity() const noexcept { return size_; }

  /** Accesses the `i`th byte of the queue, with 0 being the front. */
  byte& operator[](std::size_t i) noexcept { return data_[(first_byte_ + i) & (size_ - 1)]; }
  /** \overload */
  const byte& operator[](std::size_t i) const noexcept { return data_[(first_byte_ + i) & (size_ - 1)]; }

 private:
  /** Allocated data. */
  unsigned char *data_;
  /** Size of allocated data. */
  std::size_t size_;
  /** Number of bytes in the queue. */
  std::size_t used_ = 0;
  /** Offset of the first (earliest inserted) byte. */
  std::size_t first_byte_ = 0;

  /** Constructs a view for a region. */
  std::pair<byte_view, byte_view> view_from(std::size_t start, std::size_t view_size) {
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

  /** Resizes the backing storage. \p new_size must be a power of two and >= \p used_. */
  void resize(std::size_t new_size);
};

} // namespace base

#endif // BASE_BUFFER_H_

// Local Variables:
// mode: c++
// End:
