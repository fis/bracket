/** \file
 * Protobuf input stream for brotli decompression.
 */

#ifndef PROTO_BROTLI_H_
#define PROTO_BROTLI_H_

#include <cstdint>
#include <memory>
#include <vector>

#include <brotli/decode.h>

#include <google/protobuf/io/zero_copy_stream.h>
#include <google/protobuf/stubs/common.h>

namespace proto {

/** Wrapper input stream for transparent brotli decompression. */
class BrotliInputStream : public google::protobuf::io::ZeroCopyInputStream {
 public:
  /** Constructs a wrapped file input stream. */
  static std::unique_ptr<BrotliInputStream> FromFile(const char* path);

  /**
   * Wraps \p stream with a brotli decompressor.
   *
   * If \p owned is `true`, the stream will be destroyed when this object is.
   */
  BrotliInputStream(google::protobuf::io::ZeroCopyInputStream* stream, bool owned);
  /** Wraps \p stream with a brotli decompressor, taking ownership of it. */
  BrotliInputStream(std::unique_ptr<google::protobuf::io::ZeroCopyInputStream> stream)
      : BrotliInputStream(stream.release(), /* owned: */ true) {}
  ~BrotliInputStream();

  /** Implements google::protobuf::io::ZeroCopyInputStream::Next(). */
  bool Next(const void** data, int* size) override;
  /** Implements google::protobuf::io::ZeroCopyInputStream::BackUp(). */
  void BackUp(int count) override;
  /** Implements google::protobuf::io::ZeroCopyInputStream::Skip(). */
  bool Skip(int count) override;
  /** Implements google::protobuf::io::ZeroCopyInputStream::ByteCount(). */
  google::protobuf::int64 ByteCount() const override;

 private:
  google::protobuf::io::ZeroCopyInputStream* stream_;
  bool owned_;

  const std::uint8_t* stream_chunk_;
  std::size_t stream_chunk_available_;

  std::vector<std::uint8_t> data_buffer_;
  std::uint8_t* data_;
  std::uint8_t* data_end_;
  std::size_t data_space_;

  google::protobuf::int64 byte_count_;

  struct BrotliDecoderStateDeleter {
    void operator()(BrotliDecoderState* state) {
      BrotliDecoderDestroyInstance(state);
    }
  };
  using BrotliDecoderStatePtr = std::unique_ptr<BrotliDecoderState, BrotliDecoderStateDeleter>;
  BrotliDecoderStatePtr brotli_;
};

} // namespace proto

#endif // PROTO_BROTLI_H_

// Local Variables:
// mode: c++
// End:
