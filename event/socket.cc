#include <chrono>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>

#include <openssl/ssl.h>

#include "base/callback.h"
#include "base/common.h"
#include "event/socket.h"

extern "C" {
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
}

namespace event {

namespace internal {

// Logging and error message support.

LOG_TYPE(struct addrinfo, log, ai) {
  char host[256], port[32];
  if (getnameinfo(ai.ai_addr, ai.ai_addrlen,
                  host, sizeof host, port, sizeof port,
                  NI_NUMERICHOST | NI_NUMERICSERV) == 0)
    log << host << ':' << port;
  else
    log << "(unparseable)";
  return log;
}

inline std::string ErrnoMessage(int errno_value) {
  std::string message = std::generic_category().message(errno_value);
  message += " (";
  message += std::to_string(errno_value);
  message += ')';
  return message;
}

// Basic TCP socket.

class BasicSocket : public Socket, public FdReader, public FdWriter {
 public:
  BasicSocket(const Socket::Builder& opt);
  DISALLOW_COPY(BasicSocket);
  ~BasicSocket();

  void StartRead() override;
  void StartWrite() override;
  std::size_t Read(void* buf, std::size_t count) override;
  std::size_t Write(void* buf, std::size_t count) override;

  void WatchRead(bool watch);
  void WatchWrite(bool watch);

  int fd() const noexcept { return socket_; }

 private:
  enum State {
    kResolving,
    kConnecting,
    kOpen,
    kFailed,
  };

  struct AddrinfoDeleter {
    void operator()(struct addrinfo* addrs) {
      freeaddrinfo(addrs);
    }
  };

  using AddrinfoPtr = std::unique_ptr<struct addrinfo, AddrinfoDeleter>;

  struct ResolveData {
    /** Constructs a new resolve data block for resolving host \p h, port \p p. */
    ResolveData(BasicSocket* s, const std::string& h, const std::string& p)
        : socket(s), host(h), port(p)
    {}

    /**
     * Pointer to the associated connection. If the name resolution is abandoned, this field may be
     * `nullptr`. It will only be modified while #mutex is held, so that the thread can be sure the
     * client event doesn't get destroyed when it's trying to signal the results. */
    BasicSocket* socket;
    const std::string host;
    const std::string port;

    /** Pointer to a getaddrinfo(2) result list, once the results are ready. */
    AddrinfoPtr addrs;
    /** Error message from name resolution, if it failed. */
    std::string error;

    /** Mutex guarding the #connection field. */
    std::mutex mutex;
  };

  Loop* loop_;
  base::CallbackPtr<Watcher> watcher_;

  State state_;

  /** Shared state between the main and name resolution threads, only set in `kResolving` state. */
  std::shared_ptr<ResolveData> resolve_data_;
  /** Client event for posting the name resolution result. */
  /** Name resolution timeout in milliseconds. */
  int resolve_timeout_ms_;
  /** Timer for timing out the name resolution thread, only valid in `kResolving` state. */
  event::TimerId resolve_timer_;

  /** Result list from name resolution. Only valid in `kConnecting` state. */
  AddrinfoPtr connect_addrs_;
  /** Address we're currently trying to connect to, in `kConnecting` state. */
  struct addrinfo* connect_addr_;
  /** Connection timeout in milliseconds. */
  int connect_timeout_ms_;
  /** Timer for timing out the connection attempt, only valid in `kConnecting` state. */
  event::TimerId connect_timer_;

  /** TCP socket, only valid (not -1) in `kOpen` state. */
  int socket_;

  /** `true` if there's a read operation pending. */
  bool read_started_;
  /** `true` if there's a write operation pending. */
  bool write_started_;

  /**
   * Performs a blocking hostname resolution (in a separate thread).
   *
   * This method is intended to be called in a separate thread. It will take shared ownership of the
   * \p data block, and use that to communicate the results back.
   */
  static void Resolve(std::shared_ptr<ResolveData> data);
  /** Called when name resolution thread signals resolve data is ready. */
  void Resolved(long);
  /** Called if the name resolution timeout expires. */
  void ResolveTimeout();

  /** Called to start a connection attempt to the current address. */
  void Connect();
  /** Called when the asynchronous connect operation has finished. */
  void ConnectDone();
  /** Called if the connection attempt timeout expires. */
  void ConnectTimeout();
  /** Called to skip to the next available address when connection fails. */
  void ConnectNext(const std::string& error);

  /** Called when the underlying socket is ready to read, according to poll. */
  void CanRead(int fd) override;
  /** Called when the underlying socket is ready to write, according to poll. */
  void CanWrite(int fd) override;

  event::ClientLong<BasicSocket, &BasicSocket::Resolved> resolved_callback_;
  event::TimedM<BasicSocket, &BasicSocket::ResolveTimeout> resolve_timeout_callback_;
  event::TimedM<BasicSocket, &BasicSocket::ConnectTimeout> connect_timeout_callback_;
};

BasicSocket::BasicSocket(const Socket::Builder& opt)
    : loop_(opt.loop_), watcher_(opt.watcher_),
      state_(kResolving),
      resolve_timeout_ms_(opt.resolve_timeout_ms_), connect_timeout_ms_(opt.connect_timeout_ms_),
      socket_(-1),
      read_started_(false), write_started_(false),
      resolved_callback_(opt.loop_, this), resolve_timeout_callback_(this), connect_timeout_callback_(this)
{
  LOG(DEBUG) << "Resolving host: " << opt.host_ << ':' << opt.port_;

  resolve_data_ = std::make_shared<ResolveData>(this, opt.host_, opt.port_);
  resolve_timer_ = loop_->Delay(std::chrono::milliseconds(resolve_timeout_ms_), &resolve_timeout_callback_);
  std::thread(&Resolve, resolve_data_).detach();
}

BasicSocket::~BasicSocket() {
  if (resolve_data_) {
    std::shared_ptr<ResolveData> data(std::move(resolve_data_));
    std::unique_lock<std::mutex> lock(data->mutex);
    data->socket = nullptr;
  }

  if (resolve_timer_)
    loop_->CancelTimer(resolve_timer_);

  if (socket_ != -1) {
    loop_->ReadFd(socket_);
    loop_->WriteFd(socket_);
    close(socket_);
  }
}

void BasicSocket::Resolve(std::shared_ptr<ResolveData> data) {
  struct addrinfo hints;
  std::memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = 0;
  hints.ai_flags = AI_ADDRCONFIG;

  struct addrinfo *addrs;
  int ret = getaddrinfo(data->host.c_str(), data->port.c_str(), &hints, &addrs);
  if (ret != 0) {
    data->error = "getaddrinfo: ";
    const char* error = gai_strerror(ret);
    data->error += error ? error : "unknown error";
  } else if (!addrs) {
    data->error = "getaddrinfo: no addresses";
  } else {
    data->addrs = AddrinfoPtr(addrs);
  }

  std::unique_lock<std::mutex> lock(data->mutex);
  if (!data->socket)
    return;  // attempt abandoned
  data->socket->resolved_callback_(0);
}

void BasicSocket::Resolved(long) {
  CHECK(state_ == kResolving);

  connect_addrs_ = std::move(resolve_data_->addrs);
  std::string error = std::move(resolve_data_->error);
  resolve_data_.reset();
  loop_->CancelTimer(resolve_timer_);
  resolve_timer_ = event::kNoTimer;

  if (!connect_addrs_) {
    state_ = kFailed;
    watcher_.Call(&Watcher::ConnectionFailed, error);
    return;
  }

  state_ = kConnecting;
  connect_addr_ = connect_addrs_.get();
  Connect();
}

void BasicSocket::ResolveTimeout() {
  resolve_timer_ = event::kNoTimer;
  {
    std::shared_ptr<ResolveData> data(std::move(resolve_data_));
    std::unique_lock<std::mutex> lock(data->mutex);
    data->socket = nullptr;
  }
  state_ = kFailed;
  watcher_.Call(&Watcher::ConnectionFailed, "name lookup timeout");
}

void BasicSocket::Connect() {
  CHECK(connect_addr_);
  LOG(DEBUG) << "Connecting to " << *connect_addr_;

  socket_ = socket(connect_addr_->ai_family, connect_addr_->ai_socktype, connect_addr_->ai_protocol);
  if (socket_ == -1) {
    ConnectNext("socket: " + ErrnoMessage(errno));
    return;
  }
  if (fcntl(socket_, F_SETFL, O_NONBLOCK) == -1) {
    ConnectNext("fnctl(O_NONBLOCK): " + ErrnoMessage(errno));
    return;
  }

  int ret = connect(socket_, connect_addr_->ai_addr, connect_addr_->ai_addrlen);
  if (ret == -1 && errno == EINPROGRESS) {
    // async connect started
    loop_->WriteFd(socket_, this);
    connect_timer_ = loop_->Delay(std::chrono::milliseconds(connect_timeout_ms_), &connect_timeout_callback_);
  } else if (ret == 0) {
    // somehow connected immediately
    ConnectDone();
  } else {
    ConnectNext("connect: " + ErrnoMessage(errno));
  }
}

void BasicSocket::ConnectDone() {
  LOG(DEBUG) << "Connected to " << *connect_addr_;

  connect_addrs_.reset();
  connect_addr_ = nullptr;

  state_ = kOpen;
  watcher_.Call(&Watcher::ConnectionOpen);
}

void BasicSocket::ConnectTimeout() {
  connect_timer_ = event::kNoTimer;
  loop_->WriteFd(socket_);
  ConnectNext("connect timed out");
}

void BasicSocket::ConnectNext(const std::string& error) {
  if (connect_timer_) {
    loop_->CancelTimer(connect_timer_);
    connect_timer_ = event::kNoTimer;
  }

  if (socket_ != -1) {
    close(socket_);
    socket_ = -1;
  }

  if (connect_addr_->ai_next) {
    LOG(WARNING)
        << "Connecting to " << *connect_addr_ << " failed (" << error
        << ") - trying next address";
    connect_addr_ = connect_addr_->ai_next;
    Connect();
    return;
  }

  state_ = kFailed;
  watcher_.Call(&Watcher::ConnectionFailed, error);
}

void BasicSocket::CanRead(int fd) {
  CHECK(fd == socket_);
  CHECK(state_ == kOpen);

  watcher_.Call(&Watcher::CanRead);
}

void BasicSocket::CanWrite(int fd) {
  CHECK(fd == socket_);
  CHECK(state_ == kConnecting || state_ == kOpen);

  if (state_ == kConnecting) {
    // async connect finished

    loop_->WriteFd(socket_);
    loop_->CancelTimer(connect_timer_);
    connect_timer_ = event::kNoTimer;

    int error;
    socklen_t error_len = sizeof error;

    if (getsockopt(socket_, SOL_SOCKET, SO_ERROR, &error, &error_len) == -1) {
      ConnectNext("getsockopt(SO_ERROR): " + ErrnoMessage(errno));
      return;
    } else if (error != 0) {
      ConnectNext("connect: " + ErrnoMessage(error));
      return;
    }

    ConnectDone();
    return;
  }

  watcher_.Call(&Watcher::CanWrite);
}

void BasicSocket::StartRead() {
  CHECK(!read_started_);

  read_started_ = true;
  WatchRead(true);
}

void BasicSocket::StartWrite() {
  CHECK(!write_started_);

  write_started_ = true;
  WatchWrite(true);
}

void BasicSocket::WatchRead(bool watch) {
  CHECK(state_ == kOpen);

  if (watch)
    loop_->ReadFd(socket_, this);
  else
    loop_->ReadFd(socket_);
}

void BasicSocket::WatchWrite(bool watch) {
  CHECK(state_ == kOpen);

  if (watch)
    loop_->WriteFd(socket_, this);
  else
    loop_->WriteFd(socket_);
}

std::size_t BasicSocket::Read(void* buf, std::size_t count) {
  CHECK(state_ == kOpen);

  ssize_t ret = read(socket_, buf, count);

  if (ret == -1 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
    return 0;  // can't read any data without blocking

  if (ret == -1)
    throw Exception("read", errno);
  if (ret == 0)
    throw Exception("read: EOF");

  if (read_started_) {
    read_started_ = false;
    loop_->ReadFd(socket_);
  }
  return ret;
}

std::size_t BasicSocket::Write(void* buf, std::size_t count) {
  CHECK(state_ == kOpen);

  ssize_t ret = write(socket_, buf, count);

  if (ret == -1 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
    return 0;  // can't write any data without blocking

  if (ret == -1)
    throw Exception("write", errno);

  if (write_started_) {
    write_started_ = false;
    loop_->WriteFd(socket_);
  }
  return ret;
}

// BoringSSL TLS socket.

class TlsSocket : public Socket, public Socket::Watcher {
 public:
  TlsSocket(const Socket::Builder& opt);
  DISALLOW_COPY(TlsSocket);
  ~TlsSocket();

  void StartRead() override;
  void StartWrite() override;
  std::size_t Read(void* buf, std::size_t count) override;
  std::size_t Write(void* buf, std::size_t count) override;

 private:
  enum PendingOp {
    kNone = 0,
    kWantReadForRead,
    kWantWriteForRead,
    kWantReadForWrite,
    kWantWriteForWrite,
  };

  BasicSocket socket_;
  base::CallbackPtr<Socket::Watcher> watcher_;

  bssl::UniquePtr<SSL_CTX> ssl_ctx_;
  bssl::UniquePtr<SSL> ssl_;

  bool read_started_;
  bool write_started_;
  bool read_watched_;
  bool write_watched_;
  PendingOp pending_;

  void ConnectionOpen() override;
  void ConnectionFailed(const std::string& error) override;
  void CanRead() override;
  void CanWrite() override;

  void WatchRead(bool watch);
  void WatchWrite(bool watch);
};

class TlsException : public Socket::Exception {
 public:
  explicit TlsException(const std::string& what, int tls_error = SSL_ERROR_SSL, int ret = -1);

 private:
  std::string Describe(const std::string& what, int tls_error, int ret) const noexcept;
};

TlsSocket::TlsSocket(const Socket::Builder& opt)
    : socket_(Socket::Builder(opt).watcher(this)),
      watcher_(opt.watcher_),
      read_started_(false), write_started_(false),
      read_watched_(false), write_watched_(false),
      pending_(kNone)
{
  ssl_ctx_ = bssl::UniquePtr<SSL_CTX>(SSL_CTX_new(TLS_method()));
  SSL_CTX_set_mode(ssl_ctx_.get(), SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);

  if (!opt.client_cert_.empty()) {
    ERR_clear_error();
    if (SSL_CTX_use_certificate_chain_file(ssl_ctx_.get(), opt.client_cert_.c_str()) != 1)
      throw TlsException("can't load client certificate: " + opt.client_cert_);

    const auto& key = opt.client_key_.empty() ? opt.client_cert_ : opt.client_key_;
    ERR_clear_error();
    if (SSL_CTX_use_PrivateKey_file(ssl_ctx_.get(), key.c_str(), SSL_FILETYPE_PEM) != 1)
      throw TlsException("can't load client private key: " + key);
  }
}

TlsSocket::~TlsSocket() {}

void TlsSocket::StartRead() {
  CHECK(ssl_);
  CHECK(!read_started_);

  read_started_ = true;
  if (pending_ == kNone)
    WatchRead(true);
}

void TlsSocket::StartWrite() {
  CHECK(ssl_);
  CHECK(!write_started_);

  write_started_ = true;
  if (pending_ == kNone)
    WatchWrite(true);
}

std::size_t TlsSocket::Read(void* buf, std::size_t count) {
  CHECK(ssl_);
  CHECK(pending_ != kWantReadForWrite && pending_ != kWantWriteForWrite);

  ERR_clear_error();
  int ret = SSL_read(ssl_.get(), buf, count);

  if (ret <= 0) {
    int error = SSL_get_error(ssl_.get(), ret);
    if (error == SSL_ERROR_WANT_READ) {
      if (!read_watched_)
        WatchRead(true);
      if (write_watched_)
        WatchWrite(false);
      pending_ = kWantReadForRead;
      return 0;
    } else if (error == SSL_ERROR_WANT_WRITE) {
      if (read_watched_)
        WatchRead(false);
      if (!write_watched_)
        WatchWrite(true);
      pending_ = kWantWriteForRead;
      return 0;
    } else {
      throw TlsException("TLS read", error, ret);
    }
  }

  read_started_ = false;
  if (read_watched_)
    WatchRead(false);

  pending_ = kNone;
  if (!write_started_ && write_watched_)
    WatchWrite(false);
  else if (write_started_ && !write_watched_)
    WatchWrite(true);

  return ret;
}

std::size_t TlsSocket::Write(void* buf, std::size_t count) {
  CHECK(ssl_);
  CHECK(pending_ != kWantReadForRead && pending_ != kWantWriteForRead);

  ERR_clear_error();
  int ret = SSL_write(ssl_.get(), buf, count);

  if (ret <= 0) {
    int error = SSL_get_error(ssl_.get(), ret);
    if (error == SSL_ERROR_WANT_READ) {
      if (!read_watched_)
        WatchRead(true);
      if (write_watched_)
        WatchWrite(false);
      pending_ = kWantReadForWrite;
      return 0;
    } else if (error == SSL_ERROR_WANT_WRITE) {
      if (read_watched_)
        WatchRead(false);
      if (!write_watched_)
        WatchWrite(true);
      pending_ = kWantReadForWrite;
      return 0;
    } else {
      throw TlsException("TLS write", error, ret);
    }
  }

  write_started_ = false;
  if (write_watched_)
    WatchWrite(false);

  pending_ = kNone;
  if (!read_started_ && read_watched_)
    WatchRead(false);
  else if (read_started_ && !read_watched_)
    WatchRead(true);

  return ret;
}

void TlsSocket::ConnectionOpen() {
  ssl_ = bssl::UniquePtr<SSL>(SSL_new(ssl_ctx_.get()));
  SSL_set_connect_state(ssl_.get());
  SSL_set_fd(ssl_.get(), socket_.fd());

  watcher_.Call(&Socket::Watcher::ConnectionOpen);
}

void TlsSocket::ConnectionFailed(const std::string& error) {
  watcher_.Call(&Socket::Watcher::ConnectionFailed, error);
}

void TlsSocket::CanRead() {
  CHECK(pending_ != kWantWriteForRead && pending_ != kWantWriteForWrite);
  CHECK(pending_ != kNone || read_started_);

  if (pending_ == kNone || pending_ == kWantReadForRead)
    watcher_.Call(&Socket::Watcher::CanRead);
  else if (pending_ == kWantReadForWrite)
    watcher_.Call(&Socket::Watcher::CanWrite);
  else
    FATAL("impossible: pending_ invalid");
}

void TlsSocket::CanWrite() {
  CHECK(pending_ != kWantReadForRead && pending_ != kWantReadForWrite);
  CHECK(pending_ != kNone || write_started_);

  if (pending_ == kNone || pending_ == kWantWriteForWrite)
    watcher_.Call(&Socket::Watcher::CanWrite);
  else if (pending_ == kWantWriteForRead)
    watcher_.Call(&Socket::Watcher::CanRead);
  else
    FATAL("impossible: pending_ invalid");
}

void TlsSocket::WatchRead(bool watch) {
  CHECK(read_watched_ != watch);
  read_watched_ = watch;
  socket_.WatchRead(watch);
}

void TlsSocket::WatchWrite(bool watch) {
  CHECK(write_watched_ != watch);
  write_watched_ = watch;
  socket_.WatchWrite(watch);
}

TlsException::TlsException(const std::string& what, int tls_error, int ret)
    : Socket::Exception(Describe(what, tls_error, ret))
{
}

extern "C" int event_tlsexception_describe_callback(const char* str, std::size_t len, void* ctx_p) {
  auto ctx = static_cast<std::pair<bool, std::string*>*>(ctx_p);

  if (ctx->first) {
    *ctx->second += ", ";
  } else {
    ctx->first = true;
    *ctx->second += " [";
  }

  *ctx->second += str;

  return 1;
}

std::string TlsException::Describe(const std::string& what, int tls_error, int ret) const noexcept {
  static const std::array<const char*, 8> messages = {
    /* SSL_ERROR_NONE: */ "success",
    /* SSL_ERROR_SSL: */ "library error",
    /* SSL_ERROR_WANT_READ: */ "need to read to progress",
    /* SSL_ERROR_WANT_WRITE: */ "need to write to progress",
    /* SSL_ERROR_WANT_X509_LOOKUP: */ "X.509 callback failed",
    /* SSL_ERROR_SYSCALL: */ nullptr,
    /* SSL_ERROR_ZERO_RETURN: */ "closed by remote host",
    /* SSL_ERROR_WANT_CONNECT: */ "transport not connected",
  };

  std::string desc(what);

  desc += ": ";
  if (tls_error == SSL_ERROR_SYSCALL && ret == 0)
    desc += "EOF";
  else if (tls_error == SSL_ERROR_SYSCALL)
    desc += ErrnoMessage(errno);
  else if (tls_error >= 0 && (unsigned)tls_error < messages.size())
    desc += messages[tls_error];
  else
    desc += "unknown TLS error";

  auto ctx = std::make_pair<bool, std::string*>(false, &desc);
  ERR_print_errors_cb(&event_tlsexception_describe_callback, &ctx);
  if (ctx.first)
    desc += ']';

  return desc;
}

} // namespace internal

Socket::Socket() {}
Socket::~Socket() {}

std::unique_ptr<Socket> Socket::Builder::Build() {
  CHECK(loop_);
  CHECK(watcher_);
  CHECK(!host_.empty());
  CHECK(!port_.empty());

  if (tls_)
    return std::make_unique<internal::TlsSocket>(*this);
  else
    return std::make_unique<internal::BasicSocket>(*this);
}

} // namespace event
