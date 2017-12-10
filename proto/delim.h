#ifndef PROTO_DELIM_H_
#define PROTO_DELIM_H_

#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <google/protobuf/message.h>

namespace proto {

class DelimReader {
 public:
  DelimReader(const char* path);
  ~DelimReader();
  bool Read(google::protobuf::Message* message, bool merge = false);

 private:
  int fd_;
  google::protobuf::io::FileInputStream in_;
};

class DelimWriter {
 public:
  DelimWriter(const char* path);
  ~DelimWriter();
  void Write(const google::protobuf::Message& message);

 private:
  int fd_;
  google::protobuf::io::FileOutputStream out_;
};

} // namespace proto

#endif // PROTO_DELIM_H_

// Local Variables:
// mode: c++
// End:
