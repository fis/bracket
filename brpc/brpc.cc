#include <google/protobuf/io/coded_stream.h>

#include "base/common.h"
#include "brpc/brpc.h"
#include "proto/util.h"

namespace brpc {

namespace {
constexpr std::size_t kMaxBytesReadAtOnce = 65536u;
constexpr std::size_t kMaxVarintLen = 10;
} // unnamed namespace

RpcCall::RpcCall(
    event::Loop* loop,
    RpcServer* server,
    std::unique_ptr<event::Socket> socket,
    RpcDispatcher* dispatcher)
    : loop_(loop), host_(server), state_(State::kDispatching), socket_(std::move(socket)), dispatcher_(dispatcher)
{
  socket_->SetWatcher(this);
  socket_->WantRead(true);
}

RpcCall::RpcCall(
    event::Loop* loop,
    RpcClient* client,
    const event::Socket::Builder& target,
    std::unique_ptr<RpcEndpoint> endpoint,
    std::uint32_t method,
    const google::protobuf::Message* message)
    : loop_(loop), host_(client), state_(State::kConnecting), endpoint_(std::move(endpoint))
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

    if (coded.HadError()) {
      Close(base::make_error("RpcCall: protobuf serialization failed"));
      return;
    }
  }

  Flush();
}

void RpcCall::Close(base::error_ptr error, bool flush) {
  if (state_ == State::kClosed)
    return;

  if (error) {
    close_error_ = std::move(error);
    flush = false;
  }

  read_buffer_.clear();
  if (state_ != State::kFlushing && flush && !write_buffer_.empty()) {
    state_ = State::kFlushing;
    socket_->WantRead(false);
    return;
  }
  write_buffer_.clear();

  state_ = State::kClosed;
  if (socket_)
    socket_.reset();

  loop_->AddFinishable(base::borrow(this));
}

void RpcCall::ConnectionOpen() {
  CHECK(state_ == State::kConnecting);
  state_ = State::kReady;

  Flush();
  read_message_ = endpoint_->RpcOpen(this);

  socket_->WantRead(true);
}

void RpcCall::ConnectionFailed(base::error_ptr error) {
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

  if (state_ == State::kDispatching) {
    if (read_buffer_.size() < 4)
      return;  // not ready to dispatch
    std::uint32_t method = read_buffer_.read_u32();

    auto endpoint = dispatcher_->RpcOpen(this, method);
    if (!endpoint) {
      Close(base::make_error("RpcCall: invalid method code: " + std::to_string(method)));
      return;
    }

    state_ = State::kReady;
    endpoint_ = std::move(endpoint);
    read_message_ = endpoint_->RpcOpen(this);
  }

  while (state_ == State::kReady) {
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
      } else {
        Close(base::make_error("RpcCall: protobuf parsing failed"));
        return;
      }
    }
  }

  if (eof) {
    if (state_ == State::kReady && read_buffer_.empty() && !message_size_.has_value())
      Close(/* error: */ nullptr, /* flush: */ false);
    else
      Close(base::make_error("RpcCall: unexpected EOF"));
  }
}

void RpcCall::CanWrite() {
  Flush();
}

void RpcCall::Flush() {
  if (state_ != State::kReady && state_ != State::kFlushing)
    return;

  if (socket_->safe_to_write()) {
    while (!write_buffer_.empty()) {
      base::byte_view chunk = write_buffer_.next();
      base::io_result wrote = socket_->Write(chunk.data(), chunk.size());
      if (!wrote.ok()) {
        Close(wrote.error());
        return;
      }
      if (wrote.size() == 0)
        break;
      write_buffer_.pop(wrote.size());
    }
  }

  socket_->WantWrite(!write_buffer_.empty());

  if (state_ == State::kFlushing && write_buffer_.empty())
    Close(/* error: */ nullptr, /* flush: */ false);
}

void RpcCall::LoopFinished() {
  // called to destroy this RpcCall without reentrancy issues

  if (endpoint_)
    endpoint_->RpcClose(this, std::move(close_error_));
  else if (dispatcher_)
    dispatcher_->RpcError(std::move(close_error_));
  else  // impossible
    LOG(ERROR) << "swallowed RPC error: " << *close_error_;

  // will self-destroy
  if (std::holds_alternative<RpcServer*>(host_))
    std::get<RpcServer*>(host_)->CloseCall(this);
  else
    std::get<RpcClient*>(host_)->CloseCall(this);
}

base::error_ptr RpcServer::Start(const std::string& path) {
  auto ret = event::ListenUnix(loop_, this, path);
  if (!ret.ok())
    return ret.error();
  socket_ = ret.ptr();
  return nullptr;
}

} // namespace brpc
