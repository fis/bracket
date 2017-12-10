#include <cerrno>
#include <cstdlib>

#include <google/protobuf/io/coded_stream.h>

#include "base/exc.h"
#include "proto/delim.h"

extern "C" {
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
}

namespace proto {

namespace {

int OpenFd(const char* path, bool write) {
  int fd = open(path, write ? O_WRONLY | O_CREAT : O_RDONLY, 0644);
  if (fd == -1) {
    std::string error("open: ");
    error += path;
    throw base::Exception(error, errno);
  }
  return fd;
}

} // unnamed namespace

DelimReader::DelimReader(const char* path) : fd_(OpenFd(path, false)), in_(fd_) {}

DelimReader::~DelimReader() {
  in_.Close();
}

bool DelimReader::Read(google::protobuf::Message* message, bool merge) {
  if (!merge)
    message->Clear();

  google::protobuf::io::CodedInputStream coded(&in_);

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

DelimWriter::DelimWriter(const char* path) : fd_(OpenFd(path, true)), out_(fd_) {}

DelimWriter::~DelimWriter() {
  out_.Close();
}

void DelimWriter::Write(const google::protobuf::Message& message) {
  const std::size_t size = message.ByteSizeLong();

  {
    google::protobuf::io::CodedOutputStream coded(&out_);

    coded.WriteVarint64(size);

    google::protobuf::uint8* buffer = coded.GetDirectBufferForNBytesAndAdvance(size);
    if (buffer)
      message.SerializeWithCachedSizesToArray(buffer);
    else
      message.SerializeWithCachedSizes(&coded);

    if (coded.HadError())
      throw base::Exception("DelimWriter::Write failed");
  }

  out_.Flush();
}

} // namespace proto
