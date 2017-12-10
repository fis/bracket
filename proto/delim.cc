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

DelimWriter::DelimWriter(const char* path) : fd_(OpenFd(path)), out_(fd_) {
}

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

int DelimWriter::OpenFd(const char* path) {
  int fd = open(path, O_WRONLY | O_CREAT, 0644);
  if (fd == -1) {
    std::string error("open: ");
    error += path;
    throw base::Exception(error, errno);
  }
  return fd;
}

} // namespace proto
