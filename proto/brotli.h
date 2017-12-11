#ifndef PROTO_BROTLI_H_
#define PROTO_BROTLI_H_

#include <cstdint>
#include <memory>
#include <vector>

#include <brotli/decode.h>

#include <google/protobuf/io/zero_copy_stream.h>
#include <google/protobuf/stubs/common.h>

namespace proto {

class BrotliInputStream : public google::protobuf::io::ZeroCopyInputStream {
 public:
  static std::unique_ptr<BrotliInputStream> FromFile(const char* path);

  BrotliInputStream(google::protobuf::io::ZeroCopyInputStream* stream, bool owned);
  BrotliInputStream(std::unique_ptr<google::protobuf::io::ZeroCopyInputStream> stream)
      : BrotliInputStream(stream.release(), /* owned: */ true) {}
  ~BrotliInputStream();

  bool Next(const void** data, int* size) override;
  void BackUp(int count) override;
  bool Skip(int count) override;
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
