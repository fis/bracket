#include <cerrno>
#include <cstdlib>
#include <string>

#include "base/exc.h"
#include "proto/util.h"

#include <google/protobuf/text_format.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>

extern "C" {
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
}

namespace proto {

namespace {

int OpenFd(const char* path, bool write) {
  int fd = open(path, write ? O_WRONLY | O_CREAT | O_APPEND : O_RDONLY, 0644);
  if (fd == -1) {
    std::string error("open: ");
    error += path;
    throw base::Exception(error, errno);
  }
  return fd;
}

} // unnamed namespace

std::unique_ptr<google::protobuf::io::FileInputStream> OpenFileInputStream(const char* path) {
  int fd = OpenFd(path, /* write: */ false);
  auto stream = std::make_unique<google::protobuf::io::FileInputStream>(fd);
  stream->SetCloseOnDelete(true);
  return stream;
}

std::unique_ptr<google::protobuf::io::FileOutputStream> OpenFileOutputStream(const char* path) {
  int fd = OpenFd(path, /* write: */ true);
  auto stream = std::make_unique<google::protobuf::io::FileOutputStream>(fd);
  stream->SetCloseOnDelete(true);
  return stream;
}

void ReadText(const char* path, google::protobuf::Message* message) {
  int fd = open(path, O_RDONLY);
  if (fd == -1) {
    std::string error("unable to open config file: ");
    error += path;
    throw base::Exception(error, errno);
  }

  google::protobuf::io::FileInputStream stream(fd);
  if (!google::protobuf::TextFormat::Parse(&stream, message)) {
    std::string error("unable to parse config file: ");
    error += path;
    throw base::Exception(error);
  }
}

} // namespace proto
