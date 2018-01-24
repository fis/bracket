/** \file
 * Protobuf input stream for brotli decompression.
 */

#ifndef PROTO_BROTLI_H_
#define PROTO_BROTLI_H_

#include <cstdint>
#include <memory>
#include <vector>

#include <brotli/decode.h>
#include <brotli/encode.h>

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
  explicit BrotliInputStream(std::unique_ptr<google::protobuf::io::ZeroCopyInputStream> stream)
      : BrotliInputStream(stream.release(), /* owned: */ true) {}
  ~BrotliInputStream();

  /** Implements google::protobuf::io::ZeroCopyInputStream::Next(). */
  bool Next(const void** data, int* size) override;
  /** Implements google::protobuf::io::ZeroCopyInputStream::BackUp(). */
  void BackUp(int count) override;
  /** Implements google::protobuf::io::ZeroCopyInputStream::Skip(). */
  bool Skip(int count) override;
  /** Implements google::protobuf::io::ZeroCopyInputStream::ByteCount(). */
  google::protobuf::int64 ByteCount() const override { return byte_count_; }

 private:
  static constexpr std::size_t kInitialBufferSize = 4096;

  google::protobuf::io::ZeroCopyInputStream* stream_;
  bool owned_;

  const std::uint8_t* stream_chunk_ = nullptr;
  std::size_t stream_chunk_available_ = 0;

  // data_buffer (pre/postcondition):
  // [  returned  |  available  |  unused  ]
  //              ^- data       ^- data_end
  // data_space = data_buffer.size() - data_end
  // - returned: uncompressed data returned from Next
  // - available: uncompressed data available (BackUp was called)
  // - unused: scratch space

  std::vector<std::uint8_t> data_buffer_ = std::vector<std::uint8_t>(kInitialBufferSize);
  std::uint8_t* data_ = nullptr;
  std::uint8_t* data_end_ = nullptr;
  std::size_t data_space_ = 0;

  google::protobuf::int64 byte_count_ = 0;

  struct BrotliDecoderStateDeleter {
    void operator()(BrotliDecoderState* state) {
      BrotliDecoderDestroyInstance(state);
    }
  };
  using BrotliDecoderStatePtr = std::unique_ptr<BrotliDecoderState, BrotliDecoderStateDeleter>;
  BrotliDecoderStatePtr brotli_;
};


/** Wrapper output stream for transparent brotli compression. */
class BrotliOutputStream : public google::protobuf::io::ZeroCopyOutputStream {
 public:
  /** Constructs a wrapped file output stream. */
  static std::unique_ptr<BrotliOutputStream> ToFile(const char* path);

  /**
   * Wraps \p stream with a brotli compressor.
   *
   * If \p owned is `true`, the stream will be destroyed when this object is.
   */
  BrotliOutputStream(google::protobuf::io::ZeroCopyOutputStream* stream, bool owned);
  /** Wraps \p stream with a brotli decompressor, taking ownership of it. */
  explicit BrotliOutputStream(std::unique_ptr<google::protobuf::io::ZeroCopyOutputStream> stream)
      : BrotliOutputStream(stream.release(), /* owned: */ true) {}
  ~BrotliOutputStream();

  /** Implements google::protobuf::io::ZeroCopyOutputStream::Next(). */
  bool Next(void** data, int* size) override;
  /** Implements google::protobuf::io::ZeroCopyOutputStream::BackUp(). */
  void BackUp(int count) override;
  /** Implements google::protobuf::io::ZeroCopyOutputStream::ByteCount(). */
  google::protobuf::int64 ByteCount() const override { return byte_count_; }
  // TODO: docs
  bool WriteAliasedRaw(const void* data, int size) override;
  bool AllowsAliasing() const override { return true; }

  /**
   * Ensures all data written so far has been passed to the underlying stream.
   *
   * You must not try to write anything more.
   */
  bool Finish();

 private:
  static constexpr std::size_t kInitialBufferSize = 4096;

  google::protobuf::io::ZeroCopyOutputStream* stream_;
  bool owned_;

  std::uint8_t* stream_chunk_ = nullptr;
  std::size_t stream_chunk_available_ = 0;

  // data_buffer (pre/postcondition):
  // [  returned  |  unused  ]
  // |------------| = data_used
  // - returned: given to client for writing from Next
  // - unused: scratch space

  std::vector<std::uint8_t> data_buffer_ = std::vector<std::uint8_t>(kInitialBufferSize);
  std::size_t data_used_ = 0;

  google::protobuf::int64 byte_count_ = 0;

  struct BrotliEncoderStateDeleter {
    void operator()(BrotliEncoderState* state) {
      BrotliEncoderDestroyInstance(state);
    }
  };
  using BrotliEncoderStatePtr = std::unique_ptr<BrotliEncoderState, BrotliEncoderStateDeleter>;
  BrotliEncoderStatePtr brotli_;

  bool Compress(const std::uint8_t* data, std::size_t size);
  bool EnsureOutputAvailable();
};

} // namespace proto

#endif // PROTO_BROTLI_H_

// Local Variables:
// mode: c++
// End:
