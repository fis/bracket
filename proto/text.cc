#include <cerrno>
#include <cstdlib>
#include <string>

#include "base/exc.h"
#include "proto/text.h"

#include <google/protobuf/text_format.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>

extern "C" {
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
}

namespace proto {

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
