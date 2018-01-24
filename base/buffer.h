/** \file
 * Byte buffers and ring buffers.
 */

#ifndef BASE_BUFFER_H_
#define BASE_BUFFER_H_

#include <vector>

#include "base/log.h"

namespace base {

/** Resizable buffer of bytes. */
typedef std::vector<char> Buffer;

/** Size type for byte buffers. */
typedef Buffer::size_type BufferSize;

/** View into an existing buffer. Holds a pointer and a size. */
struct BufferView {
  /** Pointer to the data of the view. */
  char* data;
  /** Size of the pointed-to data. */
  BufferSize size;

  /** Makes a new, invalid view. */
  BufferView() : data(nullptr), size(0) {}
  /** Makes a new view from the provided parameters. */
  BufferView(char* d, BufferSize s) : data(d), size(s) {}
  /** Returns `true` if the view is pointing at valid data. */
  bool valid() { return data != nullptr; }
};

/** Automatically resizable ring buffer, for FIFO queues of bytes. */
class RingBuffer {
 public:
  /** Constructs a new ring buffer. \p initial_size must be a power of 2. */
  RingBuffer(BufferSize initial_size = 4096)
      : data_(new char[initial_size]), size_(initial_size) {
    CHECK(!(initial_size & (initial_size - 1)));
  }

  /** Destroys this buffer, freeing the memory. */
  ~RingBuffer() {
    delete[] data_;
  }

  /**
   * Allocates space for \p push_size bytes in the queue.
   *
   * The caller is responsible for writing some data. Of the two BufferView objects that are
   * returned, the first one is always valid, and has a size between 1 and \p push_size. The second
   * BufferView is only valid if the first one is smaller than the requested size, in which case
   * *its* size will be `push_size - first.size`. This happens when the requested range crosses the
   * wrap-around point of the buffer.
   */
  std::pair<BufferView, BufferView> Push(BufferSize push_size);

  /** Allocates and writes a single byte. */
  void PushChar(char c) {
    if (used_ == size_) {
      *Push(1).first.data = c;
    } else {
      BufferSize end = (first_byte_ + used_) & (size_ - 1);
      data_[end] = c;
      ++used_;
    }
  }

  /**
   * Deallocates storage from the end of the buffer.
   *
   * This method can be useful if you need to Push() something for which you know an upper bound,
   * but not the exact size. Call Push() with the upper bound size, write your contents, and then
   * Unpush() the remaining space.
   *
   * Be aware that the Push() call will ensure there is sufficient free space for the entire length,
   * potentially allocating memory. Only use this approach if the upper bound is relatively tight.
   */
  void Unpush(BufferSize size) {
    CHECK(size <= used_);
    used_ -= size;
    if (!used_)
      first_byte_ = 0;
  }
  /**
   * Returns a view to the first \p size bytes of the queue.
   *
   * The argument must be at most #size(). As with Push(), the first returned view is always valid,
   * and the second one only if the boundary is crossed. `first.size + second.size` will be equal to
   * the requested size.
   */
  std::pair<BufferView, BufferView> Front(BufferSize size) {
    CHECK(size <= used_);
    return ViewFrom(first_byte_, size);
  }

  /** Deallocates first \p pop_size bytes. Must be at most #size(). */
  void Pop(BufferSize pop_size);

  /** Resets the queue to empty. */
  void Clear() {
    used_ = 0;
    first_byte_ = 0;
  }

  /** Returns the number of bytes stored in the queue. */
  BufferSize size() { return used_; }
  /** Returns the amount of memory allocated for the queue. */
  BufferSize capacity() { return size_; }

 private:
  /** Constructs a view for a region. */
  std::pair<BufferView, BufferView> ViewFrom(BufferSize start, BufferSize view_size) {
    if (start + view_size <= size_) {
      // no wrapping
      return std::make_pair(BufferView(data_ + start, view_size), BufferView());
    } else {
      // two pieces
      BufferSize first_piece = size_ - start;
      return std::make_pair(
          BufferView(data_ + start, first_piece),
          BufferView(data_, view_size - first_piece));
    }
  }

  /** Allocated data. */
  char *data_;
  /** Size of allocated data. */
  BufferSize size_;
  /** Number of bytes in the queue. */
  BufferSize used_ = 0;
  /** Offset of the first (earliest inserted) byte. */
  BufferSize first_byte_ = 0;
};

} // namespace base

#endif // BASE_BUFFER_H_

// Local Variables:
// mode: c++
// End:
