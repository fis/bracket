#include <cerrno>
#include <cstdlib>

#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>

#include "base/exc.h"
#include "proto/delim.h"
#include "proto/util.h"

namespace proto {

DelimReader::DelimReader(google::protobuf::io::ZeroCopyInputStream* stream, bool owned)
    : stream_(stream), owned_(owned)
{}

DelimReader::DelimReader(const char* path)
    : DelimReader(OpenFileInputStream(path))
{}

DelimReader::~DelimReader() {
  if (owned_)
    delete stream_;
}

bool DelimReader::Read(google::protobuf::Message* message, bool merge) {
  if (!merge)
    message->Clear();

  google::protobuf::io::CodedInputStream coded(stream_);

  google::protobuf::uint64 size;
  if (!coded.ReadVarint64(&size))
    return false;

  google::protobuf::io::CodedInputStream::Limit limit = coded.PushLimit(size);
  bool success =
      message->MergeFromCodedStream(&coded)
      && coded.ConsumedEntireMessage();
  coded.PopLimit(limit);

  return success; // TODO: distinguish EOF from errors
}

bool DelimReader::Skip() {
  google::protobuf::io::CodedInputStream coded(stream_);

  google::protobuf::uint64 size;
  if (!coded.ReadVarint64(&size))
    return false;

  return coded.Skip(size);
}

DelimWriter::DelimWriter(google::protobuf::io::ZeroCopyOutputStream* stream, bool owned)
    : stream_(stream), owned_(owned), file_(nullptr)
{}

DelimWriter::DelimWriter(const char* path)
    : DelimWriter(nullptr, /* owned: */ false)
{
  file_ = OpenFileOutputStream(path).release();
  stream_ = file_;
  owned_ = true;
}

DelimWriter::~DelimWriter() {
  if (owned_)
    delete stream_;
}

void DelimWriter::Write(const google::protobuf::Message& message) {
  const std::size_t size = message.ByteSizeLong();

  {
    google::protobuf::io::CodedOutputStream coded(stream_);

    coded.WriteVarint64(size);

    google::protobuf::uint8* buffer = coded.GetDirectBufferForNBytesAndAdvance(size);
    if (buffer)
      message.SerializeWithCachedSizesToArray(buffer);
    else
      message.SerializeWithCachedSizes(&coded);

    if (coded.HadError())
      throw base::Exception("DelimWriter::Write failed");
  }

  if (file_)
    file_->Flush();
}

} // namespace proto
