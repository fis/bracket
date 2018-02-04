/** \file
 * Utility functions for dealing with protos.
 */

#ifndef PROTO_UTIL_H_
#define PROTO_UTIL_H_

#include <memory>

#include <google/protobuf/message.h>
#include <google/protobuf/io/zero_copy_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>

#include "base/buffer.h"
#include "base/common.h"

namespace proto {

/**
 * Opens a file descriptor and wraps it into a protobuf input stream.
 *
 * No access to the file descriptor is provided, and it will be automatically closed when the stream
 * is destroyed.
 */
std::unique_ptr<google::protobuf::io::FileInputStream> OpenFileInputStream(const char* path);

/**
 * Opens a file descriptor and wraps it into a protobuf output stream.
 *
 * No access to the file descriptor is provided, and it will be automatically closed when the stream
 * is destroyed.
 *
 * The file descriptor is opened with the `O_CREAT` and `O_APPEND` flags, with mode `0644` (modified
 * by umask).
 */
std::unique_ptr<google::protobuf::io::FileOutputStream> OpenFileOutputStream(const char* path);

/**
 * Reads a single text-format proto message from a file.
 *
 * Intended use case is for things like reading a configuration file.
 */
void ReadText(const char* path, google::protobuf::Message* message);
/** \overload */
inline void ReadText(const std::string& path, google::protobuf::Message* message) {
  ReadText(path.c_str(), message);
}

constexpr std::size_t kMaxVarintSize = 10;

template<typename B>
inline std::size_t CheckVarint(const B& buffer, std::size_t size) {
  for (std::size_t i = 0; i < size && i < kMaxVarintSize; ++i)
    if (!(buffer[i] & 0x80))
      return i + 1;
  return 0;
}

template<typename B>
inline std::uint64_t ReadKnownVarint(const B& buffer, std::size_t size) {
  std::uint64_t value = 0;
  for (std::size_t i = 0, shift = 0; i < size; ++i, shift += 7)
    value |= (std::uint64_t)(buffer[i] & 0x7f) << shift;
  return value;
}

class RingBufferInputStream : public google::protobuf::io::ZeroCopyInputStream {
 public:
  RingBufferInputStream(base::optional_ptr<base::ring_buffer> buffer) : buffer_(std::move(buffer)) {}
  ~RingBufferInputStream() { Consume(); }

  base::ring_buffer* buffer() noexcept { return buffer_.get(); }

  bool Next(const void** data, int* size) override;
  void BackUp(int count) override;
  bool Skip(int count) override;
  google::protobuf::int64 ByteCount() const noexcept override { return byte_count_; }

 private:
  base::optional_ptr<base::ring_buffer> buffer_;
  std::size_t last_read_ = 0;
  google::protobuf::int64 byte_count_ = 0;

  void Consume() {
    if (!last_read_) return;
    buffer_->pop(last_read_);
    last_read_ = 0;
  }
};

class RingBufferOutputStream : public google::protobuf::io::ZeroCopyOutputStream {
 public:
  RingBufferOutputStream(base::optional_ptr<base::ring_buffer> buffer) : buffer_(std::move(buffer)) {}

  base::ring_buffer* buffer() noexcept { return buffer_.get(); }

  bool Next(void** data, int* size) override;
  void BackUp(int count) override;
  google::protobuf::int64 ByteCount() const noexcept override { return byte_count_; }

 private:
  base::optional_ptr<base::ring_buffer> buffer_;
  google::protobuf::int64 byte_count_ = 0;
};

} // namespace proto

#endif // PROTO_UTIL_H_

// Local Variables:
// mode: c++
// End:
