/** \file
 * Plain and TLS socket integration for event::Loop.
 */

#ifndef EVENT_SOCKET_H_
#define EVENT_SOCKET_H_

#include <cstddef>

#include "base/callback.h"
#include "base/exc.h"
#include "event/loop.h"

extern "C" {
#include <sys/types.h>
#include <sys/socket.h>
}

namespace event {

/**
 * Asynchronous (plain or TLS) socket.
 *
 * See the Socket::Builder class for information on how to configure and instantiate sockets.
 */
struct Socket {
  enum Family : int;
  enum Kind : int;
  struct Watcher;
  struct Builder;

  DISALLOW_COPY(Socket);
  virtual ~Socket() {}

  /**
   * Resets the object that receives callbacks for this socket.
   *
   *  This method is mostly intended for setting the watcher when an (initially unwatched) socket is
   *  received in a ServerSocket::Watcher::Accepted() callback.
   */
  virtual void SetWatcher(Watcher* watcher) = 0;

  /**
   * Starts establishing a connection for this socket, asynchronously.
   *
   * This method must be only called once. Either one of the Watcher::ConnectionOpen() or
   * Watcher::ConnectionFailed() methods will be called once in response. Normally this happens in a
   * subsequent loop, though for a fast failure or a local socket it may be possible for the
   * callback to be invoked during this call.
   */
  virtual void Start() = 0;

  /**
   * Indicates the client is interested in reading from the socket.
   *
   * When enabled, the Watcher::CanRead() method will be called whenever any bytes are available for
   * #Read() on the socket. To avoid spinning, you should make sure you either consume the input or
   * turn the flag off again.
   */
  virtual void WantRead(bool enabled) = 0;

  /**
   * Indicates the client is interested in writing to the socket.
   *
   * You may call #Write() at any time, but it may not write any bytes if the send queue is
   * full. Enabling this will cause the Watcher::CanWrite() method to be called when you can again
   * write some bytes. To avoid spinning, you should make sure to turn the flag off if you have
   * nothing to write.
   */
  virtual void WantWrite(bool enabled) = 0;

  /**
   * Attempts to read from the socket, returning the number of bytes read.
   *
   * If there was no data immediately available, returns a success with size 0. Use the
   * WantRead(bool) method for getting a callback when to read.
   *
   * For a TLS socket, it's not safe to call this method unless #safe_to_read() is true, or as a
   * response to the Watcher::CanRead() callback. A TLS write may have been left in a pending state,
   * which must be completed before the socket can be read from again. The implementation will make
   * sure no Watcher::CanRead() callbacks are delivered until the pending write has finished.
   */
  virtual base::io_result Read(void* buf, std::size_t count) = 0;

  /**
   * Attempts to write to the socket, returning the number of bytes written.
   *
   * If no bytes could be written without blocking, returns zero. Use the WantWrite(bool) method for
   * getting a callback when writing is possible.
   *
   * For a TLS socket, it's not safe to call this method unless #safe_tow_write() is true, or as a
   * response to the Watcher::CanWrite() callback. A TLS read may have been left in a pending state,
   * which must be completed before the socket can be written to again. The implementation will make
   * sure no Watcher::CanWrite() callbacks are delivered until the pending read has finished.
   *
   * Also for a TLS socket, if you have already tried to write some bytes, you can no longer change
   * your mind afterwards. If the return value indicates that not all bytes were written, the next
   * time you call this method, you must pass in the same contents, to avoid unpredictable
   * behavior. Some of the bytes may already have been copied to the library data structures.
   */
  virtual base::io_result Write(const void* buf, std::size_t count) = 0;

  /**
   * Returns `true` if it's okay to try reading from the socket.
   *
   * This does not guarantee there are any bytes available, only that it's okay to try. It's always
   * okay to try reading from a plain socket, but TLS sockets have certain conditions. See #Read()
   * for details.
   */
  virtual bool safe_to_read() const noexcept = 0;

  /**
   * Returns `true` if it's okay to try writing to the socket.
   *
   * This does not guarantee any bytes can be written, only that it's okay to try. It's always okay
   * to try writing to a plain socket, but TLS sockets have certain conditions. See #Write() for
   * details.
   */
  virtual bool safe_to_write() const noexcept = 0;

 protected:
  Socket() {}
};

enum Socket::Family : int {
  INET,  ///< either AF_INET and AF_INET6
  UNIX,  ///< AF_UNIX
};

enum Socket::Kind : int {
  STREAM = SOCK_STREAM,
  DGRAM = SOCK_DGRAM,
  SEQPACKET = SOCK_SEQPACKET,
};

/** Callback interface for asynchronous socket IO. */
struct Socket::Watcher : public virtual base::Callback {
  /**
   * Called to indicate the connection is open.
   *
   * Either this method or ConnectionFailed() is guaranteed to be called once.
   */
  virtual void ConnectionOpen() = 0;
  /**
   * Called to indicate the connection timed out.
   *
   * Either this method or ConnectionOpen() is guaranteed to be called once.
   */
  virtual void ConnectionFailed(std::unique_ptr<base::error> error) = 0;
  /** Called to indicate that you can safely read from the socket and expect some bytes. */
  virtual void CanRead() = 0;
  /**
   * Called to indicate that you can safely write to the socket, and some bytes will likely get
   * written.
   */
  virtual void CanWrite() = 0;
};

namespace internal {
class BasicSocket;
class TlsSocket;
} // namespace internal

/** Options to construct a socket. */
class Socket::Builder {
 public:
  /**
   * Instantiates a socket using the currently set options.
   *
   * The loop() option must be set, as well as one of the target address options (either host() and
   * port(), or unix()). The Socket::Watcher object must either be set via the watcher() option, or
   * passed to the Build() call.
   */
  base::maybe_ptr<Socket> Build(Socket::Watcher* watcher = nullptr) const;

  /** Sets event loop to register the socket on (mandatory, must outlive the socket). */
  Builder& loop(Loop* v) { loop_ = v; return *this; }
  /** Gets the currently set event loop. */
  Loop* loop() const noexcept { return loop_; }
  /** Sets the callback object to use for events on the socket (mandatory, should outlive the socket). */
  Builder& watcher(Socket::Watcher* v) { watcher_ = v; return *this; }
  /** Sets the host name to connect to (mandatory unless `unix` is set). */
  Builder& host(const std::string& v) { host_ = v; return *this; }
  /** Sets the port number or service name to connect to (mandatory unless `unix` is set). */
  Builder& port(const std::string& v) { port_ = v; return *this; }
  /** Sets the path (or abstract) name for a Unix domain socket. */
  Builder& unix(const std::string& v) { unix_ = v; return *this; }
  /** Sets the kind (stream, datagram, seqpacket) of the socket. The default is stream. */
  Builder& kind(Socket::Kind v) { kind_ = v; return *this; }
  /** Enables or disables TLS on the resulting socket. Only stream sockets are supported. */
  Builder& tls(bool v) { tls_ = v; return *this; }
  /** Sets the file name to read a client certificate from. */
  Builder& client_cert(const std::string& v) { client_cert_ = v; return *this; }
  /** Sets the file name to read a client private key from. */
  Builder& client_key(const std::string& v) { client_key_ = v; return *this; }
  /** Overrides the default name resolution timeout. */
  Builder& resolve_timeout_ms(int v) { if (v) resolve_timeout_ms_ = v; return *this; }
  /** Overrides the default connect timeout. */
  Builder& connect_timeout_ms(int v) { if (v) connect_timeout_ms_ = v; return *this; }

 private:
  static constexpr int kDefaultResolveTimeoutMs = 30000;
  static constexpr int kDefaultConnectTimeoutMs = 60000;

  Loop* loop_ = nullptr;
  Socket::Watcher* watcher_ = nullptr;
  std::string host_ = "";
  std::string port_ = "";
  std::string unix_ = "";
  Socket::Kind kind_ = Socket::STREAM;
  bool tls_ = false;
  std::string client_cert_ = "";
  std::string client_key_ = "";
  int resolve_timeout_ms_ = kDefaultResolveTimeoutMs;
  int connect_timeout_ms_ = kDefaultConnectTimeoutMs;

  friend class internal::BasicSocket;
  friend class internal::TlsSocket;
};

struct ServerSocket {
  struct Watcher;

  DISALLOW_COPY(ServerSocket);
  virtual ~ServerSocket() {}

 protected:
  ServerSocket() {}
};

struct ServerSocket::Watcher : public virtual base::Callback {
  /**
   * Called when a new connection has been accepted on the server socket.
   *
   * The passed in Socket object is initially in open state, and has no watcher assigned. There's no
   * need to call Socket::Start() on it. You should call Socket::SetWatcher method on it before any
   * other asynchronous methods.
   *
   * The Socket::Watcher::ConnectionOpen() and Socket::Watcher::ConnectionFailed will never be
   * called; the connection is implicitly open after it's been accepted.
   */
  virtual void Accepted(std::unique_ptr<Socket> socket) = 0;

  /**
   * Called if the `accept(2)` system call fails. It may be a good idea to give up.
   */
  virtual void AcceptError(std::unique_ptr<base::error> error) = 0;
};

base::maybe_ptr<ServerSocket> ListenInet(
    Loop* loop,
    ServerSocket::Watcher* watcher,
    int port);

base::maybe_ptr<ServerSocket> ListenUnix(
    Loop* loop,
    ServerSocket::Watcher* watcher,
    const std::string& path,
    Socket::Kind kind = Socket::STREAM);

} // namespace event

#endif // EVENT_SOCKET_H_

// Local Variables:
// mode: c++
// End:
