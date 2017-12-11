#include "base/log.h"
#include "proto/brotli.h"
#include "proto/util.h"

namespace proto {

namespace {

constexpr std::size_t kInitialBufferSize = 4096;
constexpr std::size_t kMaxBufferSize = 16777216;

} // unnamed namespace

std::unique_ptr<BrotliInputStream> BrotliInputStream::FromFile(const char* path) {
  return std::make_unique<BrotliInputStream>(OpenFileInputStream(path));
}

BrotliInputStream::BrotliInputStream(google::protobuf::io::ZeroCopyInputStream* stream, bool owned)
    : stream_(stream), owned_(owned),
      stream_chunk_(nullptr), stream_chunk_available_(0),
      data_buffer_(kInitialBufferSize), data_(nullptr), data_end_(nullptr), data_space_(0),
      byte_count_(0),
      brotli_(BrotliDecoderCreateInstance(nullptr, nullptr, nullptr))
{}

BrotliInputStream::~BrotliInputStream() {
  if (owned_)
    delete stream_;
  else if (stream_chunk_available_ > 0)
    stream_->BackUp(stream_chunk_available_);
}

bool BrotliInputStream::Next(const void** data, int* size) {
  // decompress data, unless there's some pending from a BackUp call

  while (data_ == data_end_) {
    BrotliDecoderResult brotli_result;

    // try to decompress pending input (if any)

    while (true) {
      data_ = &data_buffer_[0];
      data_end_ = data_;
      data_space_ = data_buffer_.size();

      brotli_result = BrotliDecoderDecompressStream(
          brotli_.get(),
          &stream_chunk_available_, &stream_chunk_,
          &data_space_, &data_end_,
          nullptr);

      if (data_end_ > data_ || brotli_result != BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT)
        break;           // got some, or allocating more output size won't help
      if (data_buffer_.size() >= kMaxBufferSize)
        return false;    // impossible: ridiculous output buffer needed
      data_buffer_.resize(2 * data_buffer_.size());
    }

    if (data_end_ > data_) {
      break;             // got some data, deliver to client
    } else if (brotli_result == BROTLI_DECODER_RESULT_SUCCESS || brotli_result == BROTLI_DECODER_RESULT_ERROR) {
      // unread input that wasn't consumed
      if (stream_chunk_available_ > 0) {
        stream_->BackUp(stream_chunk_available_);
        stream_chunk_ = nullptr;
        stream_chunk_available_ = 0;
      }
      return false;      // decoder says no more output is forthcoming
    } else if (brotli_result == BROTLI_DECODER_RESULT_NEEDS_MORE_INPUT) {
      // pull more input from underlying stream
      while (stream_chunk_available_ == 0) {
        const void *chunk;
        int size;
        if (!stream_->Next(&chunk, &size))
          return false;  // no more to give
        if (size == 0)
          continue;      // try again to get some actual data
        stream_chunk_ = static_cast<const std::uint8_t*>(chunk);
        stream_chunk_available_ = size;
      }
      continue;          // try to decompress again, with new data
    } else {
      return false;      // impossible: weird return value
    }
  }

  byte_count_ += data_end_ - data_;

  *data = data_;
  *size = data_end_ - data_;
  data_ = data_end_;
  return true;
}

void BrotliInputStream::BackUp(int count) {
  // TODO: DCHECK(data_ - &data_buffer_[0] >= count)
  data_ -= count;
  byte_count_ -= count;
}

bool BrotliInputStream::Skip(int count) {
  while (count > 0) {
    const void* data;
    int size;
    if (!Next(&data, &size))
      return false;
    if (size > count) {
      BackUp(size - count);
      size = count;
    }
    count -= size;
  }
  return true;
}

google::protobuf::int64 BrotliInputStream::ByteCount() const {
  return byte_count_;
}

} // namespace proto
