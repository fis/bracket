/** \file
 * IRC connection library.
 */

#ifndef IRC_CONNECTION_H_
#define IRC_CONNECTION_H_

#include <array>
#include <memory>
#include <queue>

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
    /** Called when a new IRC message has been received. */
    virtual void MessageReceived(const Message& message) = 0;
  };

  /**
   * Constructs a new IRC connection object.
   *
   * The provided \p config is merged with the stock default configuration, so most elements are
   * optional. There must be at least one entry in the `server` list, however.
   *
   * The connection does not take ownership of the provided Looper, but the looper is expected to
   * outlive the connection.
   */
  Connection(const Config& config, event::Loop* loop);

  DISALLOW_COPY(Connection);

  /** Releases all resources held by the connection. */
  ~Connection();

  /**
   * Attempts to establish the connection. After this method is called once, the connection will try
   * to keep itself open, reconnecting to the next server in the configuration after a short delay
   * if the established connection is lost.
   */
  void Start();

  /**
   * Posts a message over the connection.
   *
   * The message may or may not be sent immediately. If a connection
   * has not been established, messages are always buffered
   * internally. If there is an active connection, the message may be
   * sent immediately, unless limited by the flood protection.
   */
  void Send(const Message& message);

  /** Adds a listener of incoming messages. */
  void AddReader(Reader* reader, bool owned = false) {
    readers_.Add(reader, owned);
  }
  /** \overload */
  void AddReader(const std::function<void(const Message&)>& callback) {
    readers_.Add(new ReaderF(callback), true);
  }

  /** Removes a listener of incoming messages. */
  bool RemoveReader(Reader* reader) {
    return readers_.Remove(reader);
  }

 private:
  CALLBACK_F1(ReaderF, Reader, MessageReceived, const Message&);

  /** IRC connection configuration proto. */
  Config config_;
  /** Currently active server in the configuration. */
  int current_server_;

  /** Looper used for scheduling and I/O. */
  event::Loop* loop_;

  /** Reconnect timer, active if `kIdle` after an error, but running. */
  event::TimerId reconnect_timer_;

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
  std::array<char, 65536> read_buffer_;
  /** Amount of bytes used for an incomplete message in front of #read_buffer_. */
  std::size_t read_buffer_used_;
  /** Listener set for incoming messages. */
  base::CallbackSet<Reader> readers_;
  /** Most recently parsed message. */
  Message read_message_;

  /** Outgoing byte buffer. */
  base::RingBuffer write_buffer_;
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
  int write_credit_;
  /** Time point when #write_credit_ was last updated. */
  event::TimerPoint write_credit_time_;
  /** `true` if we're waiting for server socket to become ready to write. */
  bool write_expected_;
  /** If we're waiting for enough credits to send, id of the timer. */
  event::TimerId write_credit_timer_;

  /** Autojoin timer, active right after a connection has been established. */
  event::TimerId autojoin_timer_;

  /** Called when the server socket has connected. */
  void ConnectionOpen() override;
  /** Called if the server socket fails to connect for any reason. */
  void ConnectionFailed(const std::string& error) override;
  /** Called when the server socket is ready to read from. */
  void CanRead() override;
  /** Called when the server socket is ready to write to. */
  void CanWrite() override;

  /** Handles an incoming message. */
  void HandleMessage(const Message& message);

  /** Called by timer when there's enough credit to try writing. */
  void WriteCreditTimer();

  /** Reverts back to idle state and starts the reconnect timer. */
  void ConnectionLost(const std::string& error);
  /** Called when it's time to try automatically reconnecting. */
  void ReconnectTimer();

  /** Callback to attempt to join channels that need to be automatically joined. */
  void AutojoinTimer();

  event::TimedM<Connection, &Connection::WriteCreditTimer> write_credit_timer_callback_;
  event::TimedM<Connection, &Connection::ReconnectTimer> reconnect_timer_callback_;
  event::TimedM<Connection, &Connection::AutojoinTimer> autojoin_timer_callback_;
};

} // namespace irc

#endif // IRC_CONNECTION_H_

// Local Variables:
// mode: c++
// End:
