#include <google/protobuf/io/coded_stream.h>

#include "brpc/brpc.h"
#include "proto/util.h"

namespace brpc {

namespace {
constexpr std::size_t kMaxBytesReadAtOnce = 65536u;
constexpr std::size_t kMaxVarintLen = 10;
} // unnamed namespace

RpcCall::RpcCall(
    RpcServer* server,
    std::unique_ptr<event::Socket> socket,
    RpcDispatcher* dispatcher)
    : host_(server), state_(State::kDispatching), socket_(std::move(socket)), dispatcher_(dispatcher)
{
  socket_->SetWatcher(this);
  socket_->StartRead();
}

RpcCall::RpcCall(
    RpcClient* client,
    const event::Socket::Builder& target,
    std::unique_ptr<RpcEndpoint> endpoint,
    std::uint32_t method,
    const google::protobuf::Message* message)
    : host_(client), state_(State::kConnecting), endpoint_(std::move(endpoint))
{
  write_buffer_.write_u32(method);
  if (message)
    Send(*message);

  auto socket = target.Build(this);
  if (!socket.ok()) {
    Close(socket.error());
    return;
  }
  socket_ = socket.ptr();
  socket_->Start();
}

RpcCall::~RpcCall() {
  if (state_ != State::kClosed)
    Close(base::make_error("RpcCall: active call destroyed"));
}

void RpcCall::Send(const google::protobuf::Message& message) {
  if (state_ != State::kConnecting && state_ != State::kReady)
    return;

  const std::size_t message_size = message.ByteSizeLong();
  const std::size_t header_size = google::protobuf::io::CodedOutputStream::VarintSize64(message_size);

  {
    proto::RingBufferOutputStream stream(base::borrow(&write_buffer_));
    google::protobuf::io::CodedOutputStream coded(&stream);

    google::protobuf::uint8* buffer = coded.GetDirectBufferForNBytesAndAdvance(header_size + message_size);
    if (buffer) {
      coded.WriteVarint64ToArray(message_size, buffer);
      message.SerializeWithCachedSizesToArray(buffer + header_size);
    } else {
      coded.WriteVarint64(message_size);
      message.SerializeWithCachedSizes(&coded);
    }

    if (coded.HadError())
      Close(base::make_error("RpcCall: protobuf serialization failed"));
  }

  Flush();
}

void RpcCall::Close(std::unique_ptr<base::error> error) {
  if (state_ == State::kClosed)
    return;
  bool dispatching = state_ == State::kDispatching;
  state_ = State::kClosed;

  if (socket_)
    socket_.reset();

  if (dispatching)
    dispatcher_->RpcError(std::move(error));
  else
    endpoint_->RpcClose(this, std::move(error));

  // will self-destroy
  if (std::holds_alternative<RpcServer*>(host_))
    std::get<RpcServer*>(host_)->CloseCall(this);
  else
    std::get<RpcClient*>(host_)->CloseCall(this);
}

void RpcCall::ConnectionOpen() {
  CHECK(state_ == State::kConnecting);
  state_ = State::kReady;

  Flush();
  read_message_ = endpoint_->RpcOpen(this);

  socket_->StartRead(); // TODO: support for permanent read mode
}

void RpcCall::ConnectionFailed(std::unique_ptr<base::error> error) {
  Close(std::move(error));
}

void RpcCall::CanRead() {
  std::size_t total_read = 0;
  bool eof = false;

  while (total_read < kMaxBytesReadAtOnce) {
    base::byte_view chunk = read_buffer_.push_free();
    base::io_result got = socket_->Read(chunk.data(), chunk.size());
    if (got.size() < chunk.size())
      read_buffer_.unpush(chunk.size() - got.size());
    if (got.at_eof()) {
      eof = true;
      break;
    } else if (got.failed()) {
      Close(got.error());
      return;
    }
    total_read += got.size();
    if (got.size() < chunk.size())
      break;  // likely no more bytes available right now
  }
  if (total_read > 0 && !eof)
    socket_->StartRead();

  if (state_ == State::kDispatching) {
    if (read_buffer_.size() < 4)
      return;  // not ready to dispatch
    std::uint32_t method = read_buffer_.read_u32();

    auto endpoint = dispatcher_->RpcOpen(method);
    if (!endpoint) {
      Close(base::make_error("RpcCall: invalid method code: " + std::to_string(method)));
      return;
    }

    state_ = State::kReady;
    endpoint_ = std::move(endpoint);
    read_message_ = endpoint_->RpcOpen(this);
  }

  while (true) {
    if (!message_size_) {
      // read the message header

      std::size_t header_size = proto::CheckVarint(read_buffer_, read_buffer_.size());
      if (header_size == 0) {
        if (read_buffer_.size() > proto::kMaxVarintSize) {
          Close(base::make_error("RpcCall: message header parse failed"));
          return;
        }
        break;  // not ready yet
      }

      message_size_ = proto::ReadKnownVarint(read_buffer_, header_size);
      read_buffer_.pop(header_size);
    } else {
      // read a proto

      if (read_buffer_.size() < *message_size_)
        break;  // not ready yet

      std::size_t message_size = *message_size_;
      message_size_.reset();

      bool success;
      {
        proto::RingBufferInputStream stream(base::borrow(&read_buffer_));
        google::protobuf::io::CodedInputStream coded(&stream);

        google::protobuf::io::CodedInputStream::Limit limit = coded.PushLimit(message_size);
        read_message_->Clear();
        success =
            read_message_->MergeFromCodedStream(&coded)
            && coded.ConsumedEntireMessage();
        coded.PopLimit(limit);
      }

      if (success) {
        endpoint_->RpcMessage(this, *read_message_);
        // TODO: bug here: the call may have been closed/destroyed as a response to the message
      } else {
        Close(base::make_error("RpcCall: protobuf parsing failed"));
        return;
      }
    }
  }

  if (eof)
    Close(
        state_ == State::kReady && read_buffer_.empty() && !message_size_.has_value()
        ? nullptr /* regular termination */
        : base::make_error("RpcCall: unexpected EOF"));
}

void RpcCall::CanWrite() {
  Flush();
}

void RpcCall::Flush() {
  if (state_ != State::kReady)
    return;

  while (!write_buffer_.empty()) {
    base::byte_view chunk = write_buffer_.next();
    base::io_result wrote = socket_->Write(chunk.data(), chunk.size());
    if (!wrote.ok()) {
      Close(wrote.error());
      return;
    }
    if (wrote.size() == 0) {
      socket_->StartWrite();
      return;
    }
    write_buffer_.pop(wrote.size());
  }
}

std::unique_ptr<base::error> RpcServer::Start(event::Loop* loop, const std::string& path) {
  auto ret = event::ListenUnix(loop, this, path);
  if (!ret.ok())
    return ret.error();
  socket_ = ret.ptr();
  return nullptr;
}

} // namespace brpc
