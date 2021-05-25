#include <cerrno>
#include <cstring>

#include "base/buffer.h"

extern "C" {
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
}

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

std::size_t ring_buffer::free_cont() const noexcept {
  if (empty())
    return size_;

  std::size_t end = (first_byte_ + used_) & (size_ - 1);
  if (first_byte_ < end)
    return size_ - end;
  else
    return first_byte_ - end;
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

byte_file::~byte_file() {
  if (fd_ != -1)
    close(fd_);
}

error_ptr byte_file::open(const char* file, open_mode mode, create_mode create) {
  int flags = 0;

  switch (mode) {
    case kRead: flags |= O_RDONLY; break;
    case kWrite: flags |= O_WRONLY; break;
    case kReadWrite: flags |= O_RDWR; break;
  }

  switch (create) {
    case kExist: break;
    case kCreate: flags |= O_TRUNC | O_CREAT; break;
    case kExclusive: flags |= O_TRUNC | O_CREAT | O_EXCL; break;
  }

  fd_ = ::open(file, flags, 0600);
  if (fd_ == -1)
    return base::make_os_error("open", errno);
  return nullptr;
}

io_result byte_file::read(std::size_t size, byte* dst) {
  std::size_t read = 0;
  while (size > 0) {
    ssize_t got = ::read(fd_, dst, size);
    if (got == -1)
      return io_result::os_error("read", errno);
    if (got == 0)
      break;
    size -= got;
    dst += got;
    read += got;
  }
  return read == 0 ? io_result::eof() : io_result::ok(read);
}

io_result byte_file::read_n(std::size_t size, byte* dst) {
  io_result ret = read(size, dst);
  if (ret.at_eof() || (ret.ok() && ret.size() < size))
    return io_result::os_error("read: truncated");
  else
    return ret;
}

io_result byte_file::read_at(std::size_t offset, std::size_t size, byte* dst) const {
  std::size_t read = 0;
  while (size > 0) {
    ssize_t got = pread(fd_, dst, size, offset);
    if (got == -1)
      return io_result::os_error("pread", errno);
    if (got == 0)
      break;
    offset += got;
    size -= got;
    dst += got;
    read += got;
  }
  return read == 0 ? io_result::eof() : io_result::ok(read);
}

io_result byte_file::read_n_at(std::size_t offset, std::size_t size, byte* dst) const {
  io_result ret = read_at(offset, size, dst);
  if (ret.at_eof() || (ret.ok() && ret.size() < size))
    return io_result::os_error("pread: truncated");
  else
    return ret;
}

io_result byte_file::write(std::size_t size, const byte* src) {
  ssize_t wrote = ::write(fd_, src, size);
  if (wrote == -1)
    return io_result::os_error("write", errno);
  return io_result::ok(wrote);
}

io_result byte_file::write_n(std::size_t size, const byte* src) {
  std::size_t left = size;
  while (left > 0) {
    ssize_t put = ::write(fd_, src, left);
    if (put == -1)
      return io_result::os_error("write", errno);
    left -= put;
    src += put;
  }
  return io_result::ok(size);
}

io_result byte_file::write_at(std::size_t offset, std::size_t size, const byte* src) {
  ssize_t wrote = pwrite(fd_, src, size, offset);
  if (wrote == -1)
    return io_result::os_error("pwrite", errno);
  return io_result::ok(wrote);
}

io_result byte_file::write_n_at(std::size_t offset, std::size_t size, const byte* src) {
  std::size_t left = size;
  while (left > 0) {
    ssize_t put = pwrite(fd_, src, left, offset);
    if (put == -1)
      return io_result::os_error("pwrite", errno);
    offset += put;
    left -= put;
    src += put;
  }
  return io_result::ok(size);
}

io_result byte_file::read_all(byte_buffer* dst, std::size_t chunk_size) {
  std::size_t total_read = 0;

  std::size_t prev_size;
  while (true) {
    prev_size = dst->size();
    dst->resize(prev_size + chunk_size);

    auto ret = read(chunk_size, dst->data() + prev_size);
    if (ret.failed())
      return io_result::error(ret.error());
    if (ret.at_eof())
      break;

    std::size_t read_size = ret.size();
    if (read_size < chunk_size)
      dst->resize(prev_size + read_size);
    total_read += read_size;
  }
  dst->resize(prev_size);  // undo needed extra space

  if (total_read > 0)
    return io_result::ok(total_read);
  else
    return io_result::eof();
}

} // namespace base
