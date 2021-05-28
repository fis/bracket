/** \file
 * Length-delimited protobuf streams.
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

#include "base/common.h"

namespace proto {

/** Reader for a stream of length-delimited protobufs. */
class DelimReader {
 public:
  /** Constructs a reader from \p stream. */
  DelimReader(base::optional_ptr<google::protobuf::io::ZeroCopyInputStream> stream);
  /** Constructs a reader from a file. */
  explicit DelimReader(const char* path);

  /**
   * Reads the next message from the stream.
   *
   * If \p merge is set, the new message will be merged with the existing contents of \p
   * message. Otherwise, Clear() is called on it beforehand.
   *
   * \return `true` if successful, `false` if no message could be read
   */
  bool Read(google::protobuf::Message* message, bool merge = false);

  /**
   * Skips over the next message in the stream.
   *
   * \return `true` if successful, `false` if the stream ended or a read failed
   */
  bool Skip();

  /**
   * Returns the number of bytes read from the stream so far.
   *
   * This includes bytes that had already been read before this object was constructed,
   * if the reader was constructed on top of an existing stream.
   */
  std::uint64_t bytes() { return stream_->ByteCount(); }

 private:
  base::optional_ptr<google::protobuf::io::ZeroCopyInputStream> stream_;
};

/** Writer for a stream of length-delimited protobufs. */
class DelimWriter {
 public:
  /** Constructs a writer into \p stream. */
  DelimWriter(base::optional_ptr<google::protobuf::io::ZeroCopyOutputStream> stream);
  /**
   * Constructs a writer into a file.
   *
   * The file will be opened as if by OpenFileOutputStream(), i.e., it will be created if necessary
   * and opened in append mode.
   */
  explicit DelimWriter(const char* path);

  /** Writes \p message into the stream. */
  void Write(const google::protobuf::Message& message);

  /**
   * Returns the number of bytes written to the stream so far.
   *
   * This includes bytes that had already been written before this object was constructed,
   * if the writer was constructed on top of an existing stream.
   */
  std::uint64_t bytes() { return stream_->ByteCount(); }

 private:
  base::optional_ptr<google::protobuf::io::ZeroCopyOutputStream> stream_;
  google::protobuf::io::FileOutputStream* file_ = nullptr;
};

} // namespace proto

#endif // PROTO_DELIM_H_

// Local Variables:
// mode: c++
// End:
