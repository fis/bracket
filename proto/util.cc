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
#include <unistd.h>
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
  int fd = OpenFd(path, /* write: */ false);
  google::protobuf::io::FileInputStream stream(fd);
  bool success = google::protobuf::TextFormat::Parse(&stream, message);
  close(fd);

  if (!success) {
    std::string error("unable to parse proto ");
    error += message->GetDescriptor()->full_name();
    error += " from: ";
    error += path;
    throw base::Exception(error);
  }
}

bool RingBufferInputStream::Next(const void** data, int* size) {
  Consume();

  base::byte_view chunk = buffer_->next();

  if (!chunk)
    return false;

  *data = chunk.data();
  *size = chunk.size();
  last_read_ = chunk.size();
  byte_count_ += chunk.size();
  return true;
}

void RingBufferInputStream::BackUp(int raw_count) {
  unsigned count = static_cast<unsigned>(raw_count);
  CHECK(count <= last_read_);
  last_read_ -= count;
}

bool RingBufferInputStream::Skip(int raw_count) {
  unsigned count = static_cast<unsigned>(raw_count);

  Consume();

  if (count > buffer_->size()) {
    byte_count_ += buffer_->size();
    buffer_->clear();
    return false;
  }

  buffer_->pop(count);
  return true;
}

bool RingBufferOutputStream::Next(void** data, int* size) {
  base::byte_view chunk = buffer_->push_free();
  *data = chunk.data();
  *size = chunk.size();
  byte_count_ += chunk.size();
  return true;
}

void RingBufferOutputStream::BackUp(int count) {
  buffer_->unpush(count);
  byte_count_ -= count;
}

} // namespace proto
