#ifndef EVENT_SOCKET_H_
#define EVENT_SOCKET_H_

#include <cstddef>

#include "base/callback.h"
#include "base/exc.h"
#include "event/loop.h"

namespace event {

/** Asynchronous (optionally TLS-enabled) socket. */
class Socket {
 public:
  struct Watcher;
  struct Builder;

#if 0
  /**
   * Creates a new socket and starts connecting to \p host (at \p port).
   *
   * \param loop event::Loop object, which must outlive this socket
   * \param watcher callback object for receiving status information
   * \param host host name or numeric address
   * \param port port number or service name
   */
  static std::unique_ptr<Socket> Create(Loop* loop, Watcher* watcher, const std::string& host, const std::string& port);

  /**
   * Creates a new TLS socket and starts connecting to \p host (at \p port).
   *
   * Note that TLS sockets have certain requirements in how they can be used. In particular, you
   * must issue Read() and Write() calls only in response to Watcher::CanRead() and
   * Watcher::CanWrite() callbacks, in order for the implementation to properly handle transparent
   * renegotiation. You may issue a StartRead() or StartWrite() even when there is a pending
   * operation, but the corresponding callbacks may be delayed.
   *
   * \param loop event::Loop object, which must outlive this socket
   * \param watcher callback object for receiving status information
   * \param host host name or numeric address
   * \param port port number or service name
   */
  static std::unique_ptr<Socket> CreateTls(Loop* loop, Watcher* watcher, const std::string& host, const std::string& port);
#endif

  DISALLOW_COPY(Socket);
  virtual ~Socket();

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
   * If there was no data immediately available, returns zero. This does not finish the read, so
   * there is no need to call StartRead() again. The Watcher::CanRead() method will be called when
   * you should try again.
   *
   * For a TLS socket, it's not safe to call this method except as a response to the
   * Watcher::CanRead() callback. A TLS read may be left in a pending state (if renegotiation is
   * triggered). The implementation will make sure no Watcher::CanWrite() callbacks are delivered
   * until the pending read has finished.
   */
  virtual std::size_t Read(void* buf, std::size_t count) = 0;

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
  virtual std::size_t Write(void* buf, std::size_t count) = 0;

  /** Exception class for indicating socket I/O errors. */
  class Exception : public base::Exception {
   public:
    explicit Exception(const std::string& what, int errno_value = 0) : base::Exception(what, errno_value) {}
  };

 protected:
  Socket();
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
  virtual void ConnectionFailed(const std::string& error) = 0;
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

/** Class for setting arguments for a socket. */

struct Socket::Builder {
  std::unique_ptr<Socket> Build();

  Builder& loop(Loop* v) { loop_ = v; return *this; }
  Builder& watcher(Socket::Watcher* v) { watcher_ = v; return *this; }
  Builder& host(const std::string& v) { host_ = v; return *this; }
  Builder& port(const std::string& v) { port_ = v; return *this; }
  Builder& tls(bool v) { tls_ = v; return *this; }
  Builder& client_cert(const std::string& v) { client_cert_ = v; return *this; }
  Builder& client_key(const std::string& v) { client_key_ = v; return *this; }
  Builder& resolve_timeout_ms(int v) { if (v) resolve_timeout_ms_ = v; return *this; }
  Builder& connect_timeout_ms(int v) { if (v) connect_timeout_ms_ = v; return *this; }

  Loop* loop_ = nullptr;
  Socket::Watcher* watcher_ = nullptr;
  std::string host_ = "";
  std::string port_ = "";
  bool tls_ = false;
  std::string client_cert_ = "";
  std::string client_key_ = "";
  int resolve_timeout_ms_ = 30000;
  int connect_timeout_ms_ = 60000;
};

} // namespace event

#endif // EVENT_SOCKET_H_

// Local Variables:
// mode: c++
// End:
