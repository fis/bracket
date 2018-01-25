/** \file
 * Utility functions for dealing with protos.
 */

#ifndef PROTO_UTIL_H_
#define PROTO_UTIL_H_

#include <memory>

#include <google/protobuf/message.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>

namespace proto {

/**
 * Opens a file descriptor and wraps it into a protobuf input stream.
 *
 * No access to the file descriptor is provided, and it will be automatically closed when the stream
 * is destroyed.
 */
std::unique_ptr<google::protobuf::io::FileInputStream> OpenFileInputStream(const char* path);

/**
 * Opens a file descriptor and wraps it into a protobuf output stream.
 *
 * No access to the file descriptor is provided, and it will be automatically closed when the stream
 * is destroyed.
 *
 * The file descriptor is opened with the `O_CREAT` and `O_APPEND` flags, with mode `0644` (modified
 * by umask).
 */
std::unique_ptr<google::protobuf::io::FileOutputStream> OpenFileOutputStream(const char* path);

/**
 * Reads a single text-format proto message from a file.
 *
 * Intended use case is for things like reading a configuration file.
 */
void ReadText(const char* path, google::protobuf::Message* message);
/** \overload */
static inline void ReadText(const std::string& path, google::protobuf::Message* message) {
  ReadText(path.c_str(), message);
}

} // namespace proto

#endif // PROTO_UTIL_H_

// Local Variables:
// mode: c++
// End:
