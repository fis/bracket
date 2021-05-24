/** \file
 * IRC connection library.
 */

#ifndef IRC_CONNECTION_H_
#define IRC_CONNECTION_H_

#include <array>
#include <memory>
#include <queue>
#include <unordered_set>

#include <prometheus/registry.h>

#include "base/buffer.h"
#include "base/callback.h"
#include "base/common.h"
#include "event/loop.h"
#include "event/socket.h"
#include "irc/config.pb.h"
#include "irc/message.h"

namespace irc {

/** Maximum accepted IRC message size. */
constexpr std::size_t kMaxMessageSize = 512;

/**
 * IRC connection.
 *
 * **Output flood control model**
 *
 * Every millisecond, we get one unit of credit, up to a credit limit of 10000 units (which is also
 * the initial amount of credit after a connection has been opened). The cost of sending most
 * messages is 1000 + N*10, where N is the number of bytes we need to send. An additional surcharge
 * applies to some commands (nick, join, part, ping, userhost: 1000, topic, kick, mode: 2000, who:
 * 3000). We can only send a message as long as we have sufficient credit for it.
 *
 * This model is implemented by keeping the following bits of state:
 *
 * - Amount of credit C at a particular time T.
 * - A queue of bytes that need to be sent.
 * - A queue of (length, per-message cost) pairs that describe the contents of the byte queue.
 *
 * When a client wants to send a message, do the following:
 *
 * - Serialize the message to the send queue, and compute its length and cost.
 * - If the queue was previously empty, try to flush. (Otherwise we're already trying to clear the
 *   queue.)
 *
 * To flush the queue (when the socket seems ready for writing):
 *
 * - Update the credit counter (C += now-T, T = now).
 * - See how many messages we can afford to send from the queue.
 * - Try to write all those messages to the server.
 * - Charge credit for the sent data. If the last message was incomplete, charge only the per-byte
 *   cost, but leave the per-command cost to be paid.
 * - If the write stopped with EAGAIN/EWOULDBLOCK, start waiting for the descriptor to become ready
 *   for writing again.
 * - Otherwise, if the queue still has data remaining, it must be because of insufficient
 *   credit. Compute a suitable delay until we can send the first message and start a timer. Once
 *   the timer fires, try flushing again.
 *
 * If we're not in the `kConnected` state, just let the messages remain in the queue. When a
 * connection is established, try to flush the buffer. If connection is lost because of
 * connect/read/write error, clear the queue.
 */
class Connection : public event::Socket::Watcher {
 public:
  /** Callback interface for incoming messages on the connection. */
  struct Reader : public virtual base::Callback {
    /** Called when any new IRC message has been received. */
    virtual void RawReceived(const Message& message) {}
    // TODO: RawSent, RawQueued?
    /** Called when the connection to a new server is ready for use. */
    virtual void ConnectionReady(const Config::Server& server) {}
    /** Called when the connection to a current server is lost. */
    virtual void ConnectionLost(const Config::Server& server) {}
    /** Called when the nickname has been registered or changed. */
    virtual void NickChanged(const std::string& nick) {}
    /** Called when we have successfully joined a channel. */
    virtual void ChannelJoined(const std::string& channel) {}
    /** Called when we have left a channel (for any reason, including lost connection). */
    virtual void ChannelLeft(const std::string& channel) {}
  };

  /**
   * Constructs a new IRC connection object.
   *
   * The provided \p config is merged with the stock default configuration, so most elements are
   * optional. There must be at least one entry in the `server` list, however.
   *
   * The connection does not take ownership of the provided Looper, but the looper is expected to
   * outlive the connection. If the optional \p metric_registry parameter is set, it must do
   * likewise.
   */
  Connection(const Config& config, event::Loop* loop, prometheus::Registry* metric_registry = nullptr, const std::map<std::string, std::string>& metric_labels = std::map<std::string, std::string>());

  DISALLOW_COPY(Connection);

  /** Releases all resources held by the connection. */
  ~Connection();

  /**
   * Attempts to establish the connection. After this method is called once, the connection will try
   * to keep itself open, reconnecting to the next server in the configuration after a short delay
   * if the established connection is lost.
   */
  void Start();

  // TODO: Stop() w/ a quit message -- need to figure out how to terminate.
  void Stop();

  /**
   * Posts a message over the connection.
   *
   * If the connection isn't ready for use, messages may be dropped. This is to
   * avoid the situation where a lot of messages end up queued, and would then
   * be flushed to a channel after connectivity is restored. Further, flood
   * control may delay messages even if the connection is ready.
   */
  void Send(const Message& message);

  /** Adds a listener of incoming messages. */
  void AddReader(base::optional_ptr<Reader> reader) {
    readers_.Add(std::move(reader));
  }

  /** Removes a listener of incoming messages. */
  bool RemoveReader(Reader* reader) {
    return readers_.Remove(reader);
  }

 private:
  /** Maximum number of write credits. */
  static constexpr int kMaxWriteCredit = 10000;

  /** IRC connection configuration proto. */
  Config config_;
  /** Currently active server in the configuration. */
  int current_server_ = 0;

  /** Looper used for scheduling and I/O. */
  event::Loop* loop_;

  // Prometheus metrics.
  prometheus::Gauge* metric_connection_up_ = nullptr;
  prometheus::Counter* metric_sent_bytes_ = nullptr;
  prometheus::Counter* metric_sent_lines_ = nullptr;
  prometheus::Counter* metric_received_bytes_ = nullptr;
  prometheus::Counter* metric_received_lines_ = nullptr;
  prometheus::Gauge* metric_write_queue_bytes_ = nullptr;

  /** Reconnect timer, active if `kIdle` after an error, but running. */
  event::TimerId reconnect_timer_ = event::kNoTimer;

  /**
   * Connection state enumeration.
   *
   * The connection is initially in the disconnected state, and may revert back
   * to it if the existing socket dies. Once a socket is constructed, the
   * connection moves to the connecting state, during which it performs the
   * nickname registration and waits for the server to send the usual numerics
   * that signal the connection is ready for use. At that point, the connection
   * moves to the registered state, but still waits for a signal that it's a
   * good time to join the auto-join channels. Once that has happened, the
   * connection finally moves to the ready state.
   */
  enum State {
    kDisconnected,
    kConnecting,
    kRegistered,
    kReady,
  };
  /** Connection state. */
  State state_ = kDisconnected;
  /** Server socket. */
  std::unique_ptr<event::Socket> socket_;

  /**
   * Buffer for incoming data.
   *
   * Outside callbacks, this buffer holds at most one incomplete
   * message. Complete messages have already been sent to callbacks at
   * that point.
   *
   * To avoid deadlocks, this buffer must be at least #kMaxMessageSize
   * (512) bytes. Otherwise an incomplete message may fill the buffer,
   * preventing further reads from taking place.
   */
  std::array<unsigned char, 65536> read_buffer_;
  /** Amount of bytes used for an incomplete message in front of #read_buffer_. */
  std::size_t read_buffer_used_ = 0;
  /** Listener set for incoming messages. */
  base::CallbackSet<Reader> readers_;
  /** Most recently parsed message. */
  Message read_message_;

  /** Outgoing byte buffer. */
  base::ring_buffer write_buffer_;
  /**
   * Outgoing message queue, describing #write_buffer_ contents.
   *
   * The elements are (bytes, cost) pairs, where the first element is
   * the message length (up to #kMaxMessageSize), and the second
   * element is the per-message cost component (not including the
   * per-byte cost).
   */
  std::deque<std::pair<int, int>> write_queue_;
  /** Available write credits, as of #write_credit_time_. */
  int write_credit_ = kMaxWriteCredit;
  /** Time point when #write_credit_ was last updated. */
  event::TimerPoint write_credit_time_ = loop_->now();
  /** `true` if we're waiting for server socket to become ready to write. */
  bool write_expected_ = false;
  /** If we're waiting for enough credits to send, id of the timer. */
  event::TimerId write_credit_timer_ = event::kNoTimer;

  /** Currently active nickname. May not match config_.nick() if unavailable. */
  std::string nick_;
  /** If nonzero, #nick_ is not the configured nick. The number is used as a suffix. */
  int alt_nick_ = 0;
  /** If we're waiting to reclaim the configured nickname, id of the timer. */
  event::TimerId nick_regain_timer_ = event::kNoTimer;

  /** Possible states configured channels can be in. */
  enum class ChannelState {
    /** Registered in configuration, but not joined or attempting to join yet. */
    kKnown,
    /** Join command sent (or in send queue), waiting for response. */
    kJoining,
    /** Currently on the channel. */
    kJoined,
  };
  /** States configured channels are in. */
  std::unordered_map<std::string, ChannelState> channels_;
  /** If we're waiting to auto-join channels, id of the timer. */
  event::TimerId auto_join_timer_ = event::kNoTimer;

  /** Called when the server socket has connected. */
  void ConnectionOpen() override;
  /** Called if the server socket fails to connect for any reason. */
  void ConnectionFailed(base::error_ptr error) override;
  /** Called when the server socket is ready to read from. */
  void CanRead() override;
  /** Called when the server socket is ready to write to. */
  void CanWrite() override;

  /**
   * Posts a message over the connection.
   *
   * The message may or may not be sent immediately. If a connection has not been established,
   * messages are always buffered internally. If there is an active connection, the message may be
   * sent immediately, unless limited by the flood protection.
   */
  void SendNow(const Message& message);
  /** Tries to flush as much of the send buffer as possible. */
  void Flush();

  /** Handles an incoming message. */
  void HandleMessage(const Message& message);

  /** Called by timer when there's enough credit to try writing. */
  void WriteCreditTimer();

  /** Reverts back to idle state and starts the reconnect timer. */
  void ConnectionLost(base::error_ptr error);
  /** Called when it's time to try automatically reconnecting. */
  void ReconnectTimer();

  /** Called when nick registration is successful. */
  void Registered();
  /** Callback to do actions (like autojoin) after connecting to a new server. */
  void AutoJoinTimer();
  /** Callback to attempt to regain the configured nickname. */
  void NickRegainTimer();

  event::TimedM<Connection, &Connection::WriteCreditTimer> write_credit_timer_callback_{this};
  event::TimedM<Connection, &Connection::ReconnectTimer> reconnect_timer_callback_{this};
  event::TimedM<Connection, &Connection::AutoJoinTimer> auto_join_timer_callback_{this};
  event::TimedM<Connection, &Connection::NickRegainTimer> nick_regain_timer_callback_{this};
};

} // namespace irc

#endif // IRC_CONNECTION_H_

// Local Variables:
// mode: c++
// End:
