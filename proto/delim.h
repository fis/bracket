#ifndef PROTO_DELIM_H_
#define PROTO_DELIM_H_

#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <google/protobuf/message.h>

namespace proto {

class DelimWriter {
 public:
  DelimWriter(const char* path);
  ~DelimWriter();
  void Write(const google::protobuf::Message& message);

 private:
  int fd_;
  google::protobuf::io::FileOutputStream out_;

  int OpenFd(const char* path);
};

class DelimReader {
  // TODO implement
};

} // namespace proto

#endif // PROTO_DELIM_H_

// Local Variables:
// mode: c++
// End:
