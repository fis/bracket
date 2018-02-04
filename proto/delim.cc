#include <cerrno>
#include <cstdlib>

#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>

#include "base/exc.h"
#include "proto/delim.h"
#include "proto/util.h"

namespace proto {

DelimReader::DelimReader(base::optional_ptr<google::protobuf::io::ZeroCopyInputStream> stream)
    : stream_(std::move(stream))
{}

DelimReader::DelimReader(const char* path)
    : DelimReader(base::own(OpenFileInputStream(path)))
{}

bool DelimReader::Read(google::protobuf::Message* message, bool merge) {
  if (!merge)
    message->Clear();

  google::protobuf::io::CodedInputStream coded(stream_.get());

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
  google::protobuf::io::CodedInputStream coded(stream_.get());

  google::protobuf::uint64 size;
  if (!coded.ReadVarint64(&size))
    return false;

  return coded.Skip(size);
}

DelimWriter::DelimWriter(base::optional_ptr<google::protobuf::io::ZeroCopyOutputStream> stream)
    : stream_(std::move(stream))
{}

DelimWriter::DelimWriter(const char* path) {
  auto file = OpenFileOutputStream(path);
  file_ = file.get();
  stream_ = base::own(std::move(file));
}

void DelimWriter::Write(const google::protobuf::Message& message) {
  const std::size_t message_size = message.ByteSizeLong();
  const std::size_t header_size = google::protobuf::io::CodedOutputStream::VarintSize64(message_size);

  {
    google::protobuf::io::CodedOutputStream coded(stream_.get());

    google::protobuf::uint8* buffer = coded.GetDirectBufferForNBytesAndAdvance(header_size + message_size);
    if (buffer) {
      coded.WriteVarint64ToArray(message_size, buffer);
      message.SerializeWithCachedSizesToArray(buffer + header_size);
    } else {
      coded.WriteVarint64(message_size);
      message.SerializeWithCachedSizes(&coded);
    }

    if (coded.HadError())
      throw base::Exception("DelimWriter::Write failed");
  }

  if (file_)
    file_->Flush();
}

} // namespace proto
