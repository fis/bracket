#ifndef BRPC_BRPC_H_
#define BRPC_BRPC_H_

/** \file RPC machinery.
 *
 * This file contains the low-level guts of the 'brpc' simple proto-based RPC scheme. Normally you
 * would interact with this code via wrappers automatically generated from a .proto service
 * definition. TODO: write detailed docs.
 */

#include <memory>
#include <optional>

#include <google/protobuf/message.h>

#include "base/buffer.h"
#include "base/common.h"
#include "base/unique_set.h"
#include "event/socket.h"

namespace brpc {

class RpcCall;

/** Endpoint interface for receiving messages from an active call. */
struct RpcEndpoint {
  virtual std::unique_ptr<google::protobuf::Message> RpcOpen(RpcCall* call) = 0;
  virtual void RpcMessage(RpcCall* call, const google::protobuf::Message& message) = 0;
  virtual void RpcClose(RpcCall* call, std::unique_ptr<base::error> error) = 0;

  RpcEndpoint() = default;
  DISALLOW_COPY(RpcEndpoint);
  virtual ~RpcEndpoint() = default;
};

/** Server-side dispatcher interface returning endpoints for incoming method calls. */
struct RpcDispatcher {
  virtual std::unique_ptr<RpcEndpoint> RpcOpen(std::uint32_t method) = 0;
  virtual void RpcError(std::unique_ptr<base::error> error) = 0;

  RpcDispatcher() = default;
  DISALLOW_COPY(RpcDispatcher);
  virtual ~RpcDispatcher() = default;
};

class RpcServer;
class RpcClient;

/** Active RPC call. Handles both the client and server ends. */
class RpcCall : public event::Socket::Watcher {
 public:
  RpcCall(
      RpcServer* server,
      std::unique_ptr<event::Socket> socket,
      RpcDispatcher* dispatcher);

  RpcCall(
      RpcClient* client,
      const event::Socket::Builder& target,
      std::unique_ptr<RpcEndpoint> endpoint,
      std::uint32_t method,
      const google::protobuf::Message* message);

  DISALLOW_COPY(RpcCall);

  ~RpcCall();

  void Send(const google::protobuf::Message& message);
  void Close(std::unique_ptr<base::error> error = nullptr); // TODO: fix flushing outgoing messages on close

  void ConnectionOpen() override;
  void ConnectionFailed(std::unique_ptr<base::error> error) override;
  void CanRead() override;
  void CanWrite() override;

 private:
  std::variant<RpcServer*, RpcClient*> host_;

  enum class State { kConnecting, kDispatching, kReady, kClosed };
  State state_;

  std::unique_ptr<event::Socket> socket_;

  RpcDispatcher* dispatcher_ = nullptr;
  std::unique_ptr<RpcEndpoint> endpoint_ = nullptr;

  base::ring_buffer read_buffer_;
  base::ring_buffer write_buffer_;

  std::optional<std::size_t> message_size_;  // set if we should read a message next
  std::unique_ptr<google::protobuf::Message> read_message_;

  void Flush();
};

class RpcServer : public event::ServerSocket::Watcher {
 public:
  RpcServer(RpcDispatcher* dispatcher) : dispatcher_(dispatcher) {}

  std::unique_ptr<base::error> Start(event::Loop* loop, const std::string& path);

  void Accepted(std::unique_ptr<event::Socket> socket) override {
    calls_.emplace(this, std::move(socket), dispatcher_);
  }

  void AcceptError(std::unique_ptr<base::error> error) override {
    // TODO: consider differentiating this as a fatal error
    dispatcher_->RpcError(std::move(error));
  }

 private:
  friend class RpcCall;

  RpcDispatcher* dispatcher_;
  std::unique_ptr<event::ServerSocket> socket_;
  base::unique_set<RpcCall> calls_;

  void CloseCall(RpcCall* call) { calls_.erase(call); }
};

class RpcClient {
 public:
  RpcClient() = default;

  RpcCall* Call(std::unique_ptr<RpcEndpoint> endpoint, std::uint32_t method, const google::protobuf::Message* message) {
    return calls_.emplace(this, target_, std::move(endpoint), method, message);
  }

  event::Socket::Builder& target() { return target_; }

 private:
  friend class RpcCall;

  event::Socket::Builder target_;
  base::unique_set<RpcCall> calls_;

  void CloseCall(RpcCall* call) { calls_.erase(call); }
};

} // namespace brpc

#endif // BRPC_BRPC_H_

// Local Variables:
// mode: c++
// End:
