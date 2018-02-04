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
   * Initiates an asynchronous read operation.
   *
   * The Watcher::CanRead() method will be called when you should call Read() on the socket. A
   * successful read (one that returns any bytes, even if less than requested) will finish the
   * operation, and you must call StartRead() again if you want to keep reading.
   */
  virtual void StartRead() = 0;
  /**
   * Initiates an asynchronous write operation.
   *
   * The Watcher::CanWrite() method will be called when you should call Write() on the socket. A
   * successful write (one that consumes any bytes, even if less than requested) will finish the
   * operation, and you must call StartWrite() again if you still have more bytes to write.
   */
  virtual void StartWrite() = 0;

  /**
   * Attempts to read from the socket, returning the number of bytes read.
   *
   * If there was no data immediately available, returns a success with size 0. This does not finish
   * the read, so there is no need to call StartRead() again. The Watcher::CanRead() method will be
   * called when you should try again.
   *
   * For a TLS socket, it's not safe to call this method except as a response to the
   * Watcher::CanRead() callback. A TLS read may be left in a pending state (if renegotiation is
   * triggered). The implementation will make sure no Watcher::CanWrite() callbacks are delivered
   * until the pending read has finished.
   */
  virtual base::io_result Read(void* buf, std::size_t count) = 0;

  /**
   * Attempts to write to the socket, returning the number of bytes written.
   *
   * If no bytes could be written without blocking, returns zero. This does not finish the write, so
   * there is no need to call StartWrite() again. The Watcher::CanWrite() method will be called when
   * you should try again.
   *
   * For a TLS socket, it's not safe to call this method except as a response to the
   * Watcher::CanWrite() callback. A TLS write may be left in a pending state (if renegotiation is
   * triggered). The implementation will make sure no Watcher::CanWrite() callbacks are delivered
   * until the pending read has finished.
   *
   * Also for a TLS socket, if you have already tried to write some bytes, you can no longer change
   * your mind afterwards. If the return value indicates that not all bytes were written, the next
   * time you call this method, you must pass in the same contents, to avoid unpredictable
   * behavior. Some of the bytes may already have been copied to the library data structures.
   */
  virtual base::io_result Write(const void* buf, std::size_t count) = 0;

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
  /**
   * Called to indicate that you must read from the socket.
   *
   * This method is guaranteed to be called at least once (as long as the remote side sends any
   * data) after issuing a Socket::StartRead() call. You should attempt to read some data with
   * Socket::Read(). If the read does not return any bytes yet, this method will be called again
   * later, at which point you should try again. If any bytes were read, this method will not be
   * called any more, until you call Socket::StartRead() again.
   */
  virtual void CanRead() = 0;
  /**
   * Called to indicate that you must write to the socket.
   *
   * This method is guaranteed to be called at least once after issuing a Socket::StartWrite()
   * call. You should attempt to write some data with Socket::Write(). If the write does not
   * succeed in writing any data (returns 0), this method will be called again later, at which
   * point you should try again. If any bytes were written, this method will not be called any
   * more, until you call Socket::StartWrite() again.
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
