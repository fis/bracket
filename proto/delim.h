#ifndef PROTO_DELIM_H_
#define PROTO_DELIM_H_

#include <memory>

#include <google/protobuf/message.h>
#include <google/protobuf/io/zero_copy_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>

namespace proto {

class DelimReader {
 public:
  DelimReader(google::protobuf::io::ZeroCopyInputStream* stream, bool owned);
  DelimReader(std::unique_ptr<google::protobuf::io::ZeroCopyInputStream> stream)
      : DelimReader(stream.release(), /* owned: */ true) {}
  DelimReader(const char* path);
  ~DelimReader();
  bool Read(google::protobuf::Message* message, bool merge = false);

 private:
  google::protobuf::io::ZeroCopyInputStream* stream_;
  bool owned_;
};

class DelimWriter {
 public:
  DelimWriter(google::protobuf::io::ZeroCopyOutputStream* stream, bool owned);
  DelimWriter(std::unique_ptr<google::protobuf::io::ZeroCopyOutputStream> stream)
      : DelimWriter(stream.release(), /* owned: */ true) {}
  DelimWriter(const char* path);
  ~DelimWriter();
  void Write(const google::protobuf::Message& message);

 private:
  google::protobuf::io::ZeroCopyOutputStream* stream_;
  bool owned_;
  google::protobuf::io::FileOutputStream* file_;
};

} // namespace proto

#endif // PROTO_DELIM_H_

// Local Variables:
// mode: c++
// End:
