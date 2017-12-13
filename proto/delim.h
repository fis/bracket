/** \file
 * Implementation of length-delimited protobuf streams.
 *
 * The format is exceedingly simple: a varint-encoded message length, followed by the corresponding
 * serialized message bytes, and then the same for the next message and so on.
 */

#ifndef PROTO_DELIM_H_
#define PROTO_DELIM_H_

#include <memory>

#include <google/protobuf/message.h>
#include <google/protobuf/io/zero_copy_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>

namespace proto {

/** Reader for a stream of length-delimited protobufs. */
class DelimReader {
 public:
  /**
   * Constructs a reader from \p stream.
   *
   * If \p owned is `true`, the stream will be destroyed when this object is.
   */
  DelimReader(google::protobuf::io::ZeroCopyInputStream* stream, bool owned);
  /** Constructs a reader from \p stream, taking ownership of it. */
  DelimReader(std::unique_ptr<google::protobuf::io::ZeroCopyInputStream> stream)
      : DelimReader(stream.release(), /* owned: */ true) {}
  /** Constructs a reader from a file. */
  DelimReader(const char* path);
  ~DelimReader();

  /**
   * Reads the next message from the stream.
   *
   * If \p merge is set, the new message will be merged with the existing contents of \p
   * message. Otherwise, Clear() is called on it beforehand.
   *
   * \return `true` if successful, `false` if no message could be read
   */
  bool Read(google::protobuf::Message* message, bool merge = false);

 private:
  google::protobuf::io::ZeroCopyInputStream* stream_;
  bool owned_;
};

/** Writer for a stream of length-delimited protobufs. */
class DelimWriter {
 public:
  /**
   * Constructs a writer into \p stream.
   *
   * If \p owned is `true`, the stream will be destroyed when this object is.
   */
  DelimWriter(google::protobuf::io::ZeroCopyOutputStream* stream, bool owned);
  /** Constructs a writer into \p stream, taking ownership of it. */
  DelimWriter(std::unique_ptr<google::protobuf::io::ZeroCopyOutputStream> stream)
      : DelimWriter(stream.release(), /* owned: */ true) {}
  /**
   * Constructs a writer into a file.
   *
   * The file will be opened as if by OpenFileOutputStream(), i.e., it will be created if necessary
   * and opened in append mode.
   */
  DelimWriter(const char* path);
  ~DelimWriter();

  /** Writes \p message into the stream. */
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
