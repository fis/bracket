#ifndef PROTO_UTIL_H_
#define PROTO_UTIL_H_

#include <memory>

#include <google/protobuf/message.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>

namespace proto {

std::unique_ptr<google::protobuf::io::FileInputStream> OpenFileInputStream(const char* path);
std::unique_ptr<google::protobuf::io::FileOutputStream> OpenFileOutputStream(const char* path);

void ReadText(const char* path, google::protobuf::Message* message);

} // namespace proto

#endif // PROTO_UTIL_H_

// Local Variables:
// mode: c++
// End:
