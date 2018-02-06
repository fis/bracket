#include <chrono>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>

#include <openssl/ssl.h>

#include "base/callback.h"
#include "base/common.h"
#include "base/exc.h"
#include "event/socket.h"

extern "C" {
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
}

std::ostream& operator<<(std::ostream& os, const addrinfo& ai) {
  char host[256], port[32];
  if (getnameinfo(ai.ai_addr, ai.ai_addrlen,
                  host, sizeof host, port, sizeof port,
                  NI_NUMERICHOST | NI_NUMERICSERV) == 0)
    os << host << ':' << port;
  else
    os << "(unparseable)";
  return os;
}

namespace event {

namespace internal {

class addr_error : public base::error {
 public:
  explicit addr_error(int errcode) : errcode_(errcode) {}

  void format(std::string* str) const override;
  void format(std::ostream* str) const override;

 private:
  int errcode_;
};

void addr_error::format(std::string* str) const {
  auto err = gai_strerror(errcode_);
  *str += "getaddrinfo: "; *str += (err ? err : "unknown error");
}

void addr_error::format(std::ostream* str) const {
  auto err = gai_strerror(errcode_);
  *str << "getaddrinfo: " << (err ? err : "unknown error");
}

/**
 * Plain TCP socket.
 */
class BasicSocket : public Socket, public FdReader, public FdWriter {
 public:
  BasicSocket(const Builder& opt, Family family, Watcher* watcher);
  // Internal constructor for ServerSocket use only.
  BasicSocket(Loop* loop, int fd);

  DISALLOW_COPY(BasicSocket);
  ~BasicSocket();

  void SetWatcher(Watcher* watcher) override { watcher_.Set(base::borrow(watcher)); }

  void Start() override;
  void WantRead(bool enabled) override;
  void WantWrite(bool enabled) override;
  base::io_result Read(void* buf, std::size_t count) override;
  base::io_result Write(const void* buf, std::size_t count) override;
  bool safe_to_read() const noexcept override { return true; }
  bool safe_to_write() const noexcept override { return true; }

  // Internal interface for TlsSocket use only.
  int fd() const noexcept { return socket_; }

 private:
  enum State {
    kInitialized,
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
    /** Constructs a new resolve data block for resolving host \p h, port \p p, kind \p k. */
    ResolveData(BasicSocket* s, const std::string& h, const std::string& p, int k)
        : socket(s), host(h), port(p), kind(k)
    {}

    /**
     * Pointer to the associated connection. If the name resolution is abandoned, this field may be
     * `nullptr`. It will only be modified while #mutex is held, so that the thread can be sure the
     * client event doesn't get destroyed when it's trying to signal the results. */
    BasicSocket* socket;
    const std::string host;
    const std::string port;
    int kind;

    /** Pointer to a getaddrinfo(2) result list, once the results are ready. */
    AddrinfoPtr addrs;
    /** Error message from name resolution, if it failed. */
    base::error_ptr error;

    /** Mutex guarding the #connection field. */
    std::mutex mutex;
  };

  Loop* loop_;
  base::CallbackPtr<Watcher> watcher_;

  State state_ = kInitialized;

  /** Shared state between the main and name resolution threads, only set in `kResolving` state. */
  std::shared_ptr<ResolveData> resolve_data_;
  /** Client event for posting the name resolution result. */
  /** Name resolution timeout in milliseconds. */
  int resolve_timeout_ms_ = Socket::Builder::kDefaultResolveTimeoutMs;
  /** Timer for timing out the name resolution thread, only valid in `kResolving` state. */
  event::TimerId resolve_timer_ = kNoTimer;

  /** Result list from `getaddrinfo`. Only valid in `kConnecting` state, for an inet socket. */
  AddrinfoPtr connect_addr_inet_;
  /** Address data built explicitly. Only valid in `kConnecting` state, for a unix socket. */
  std::unique_ptr<std::pair<struct sockaddr_un, struct addrinfo>> connect_addr_unix_;
  /** Address we're currently trying to connect to, in `kConnecting` state. */
  struct addrinfo* connect_addr_ = nullptr;
  /** Connection timeout in milliseconds. */
  int connect_timeout_ms_ = Socket::Builder::kDefaultConnectTimeoutMs;
  /** Timer for timing out the connection attempt, only valid in `kConnecting` state. */
  event::TimerId connect_timer_ = kNoTimer;

  /** Socket file descriptor, only valid (not -1) in `kConnecting` or `kOpen` states. */
  int socket_ = -1;

  /** `true` if the descriptor is being polled for reading. */
  bool read_requested_ = false;
  /** `true` if the descriptor is being polled for writing. */
  bool write_requested_ = false;

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
  void ConnectNext(base::error_ptr error);

  /** Called when the underlying socket is ready to read, according to poll. */
  void CanRead(int fd) override;
  /** Called when the underlying socket is ready to write, according to poll. */
  void CanWrite(int fd) override;

  event::ClientLong<BasicSocket, &BasicSocket::Resolved> resolved_callback_{loop_, this};
  event::TimedM<BasicSocket, &BasicSocket::ResolveTimeout> resolve_timeout_callback_{this};
  event::TimedM<BasicSocket, &BasicSocket::ConnectTimeout> connect_timeout_callback_{this};
};

BasicSocket::BasicSocket(const Builder& opt, Family family, Watcher* watcher)
    : loop_(opt.loop_), watcher_(base::borrow(watcher)),
      resolve_timeout_ms_(opt.resolve_timeout_ms_), connect_timeout_ms_(opt.connect_timeout_ms_)
{
  if (family == INET) {
    resolve_data_ = std::make_shared<ResolveData>(this, opt.host_, opt.port_, opt.kind_);
  } else if (family == UNIX) {
    connect_addr_unix_ = std::make_unique<std::pair<struct sockaddr_un, struct addrinfo>>();
    struct sockaddr_un* addr = &connect_addr_unix_->first;
    connect_addr_ = &connect_addr_unix_->second;

    addr->sun_family = AF_UNIX;
    std::strncpy(addr->sun_path, opt.unix_.c_str(), sizeof addr->sun_path);

    connect_addr_->ai_family = AF_UNIX;
    connect_addr_->ai_socktype = opt.kind_;
    connect_addr_->ai_protocol = 0;
    connect_addr_->ai_addrlen = sizeof (struct sockaddr_un);
    connect_addr_->ai_addr = (struct sockaddr*) addr;
    connect_addr_->ai_next = nullptr;
  }
}

BasicSocket::BasicSocket(Loop* loop, int fd)
    : loop_(loop), state_(kOpen), socket_(fd)
{}

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

void BasicSocket::Start() {
  CHECK(state_ == kInitialized);

  if (resolve_data_) {
    LOG(DEBUG) << "resolving host: " << resolve_data_->host << ':' << resolve_data_->port;
    state_ = kResolving;
    resolve_timer_ = loop_->Delay(std::chrono::milliseconds(resolve_timeout_ms_), base::borrow(&resolve_timeout_callback_));
    std::thread(&Resolve, resolve_data_).detach();
  } else if (connect_addr_unix_) {
    state_ = kConnecting;
    Connect();
  } else {
    state_ = kFailed;
    watcher_.Call(&Watcher::ConnectionFailed, base::make_os_error("internal error"));
  }
}

void BasicSocket::Resolve(std::shared_ptr<ResolveData> data) {
  struct addrinfo hints;
  std::memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = data->kind;
  hints.ai_protocol = 0;
  hints.ai_flags = AI_ADDRCONFIG;

  struct addrinfo *addrs;
  int ret = getaddrinfo(data->host.c_str(), data->port.c_str(), &hints, &addrs);
  if (ret != 0) {
    data->error = std::make_unique<addr_error>(ret);
  } else if (!addrs) {
    data->error = base::make_os_error("getaddrinfo: no addresses");
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

  connect_addr_inet_ = std::move(resolve_data_->addrs);
  auto error = std::move(resolve_data_->error);
  resolve_data_.reset();
  loop_->CancelTimer(resolve_timer_);
  resolve_timer_ = event::kNoTimer;

  if (!connect_addr_inet_) {
    state_ = kFailed;
    watcher_.Call(&Watcher::ConnectionFailed, std::move(error));
    return;
  }

  state_ = kConnecting;
  connect_addr_ = connect_addr_inet_.get();
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
  watcher_.Call(&Watcher::ConnectionFailed, base::make_os_error("name lookup timeout"));
}

void BasicSocket::Connect() {
  CHECK(connect_addr_);
  LOG(DEBUG) << "connecting to " << *connect_addr_;

  socket_ = socket(connect_addr_->ai_family, connect_addr_->ai_socktype, connect_addr_->ai_protocol);
  if (socket_ == -1) {
    ConnectNext(base::make_os_error("socket", errno));
    return;
  }
  if (fcntl(socket_, F_SETFL, O_NONBLOCK) == -1) {
    ConnectNext(base::make_os_error("fcntl(O_NONBLOCK)", errno));
    return;
  }

  int ret = connect(socket_, connect_addr_->ai_addr, connect_addr_->ai_addrlen);
  if (ret == -1 && errno == EINPROGRESS) {
    // async connect started
    loop_->WriteFd(socket_, base::borrow(this));
    connect_timer_ = loop_->Delay(std::chrono::milliseconds(connect_timeout_ms_), base::borrow(&connect_timeout_callback_));
  } else if (ret == 0) {
    // somehow connected immediately
    ConnectDone();
  } else {
    ConnectNext(base::make_os_error("connect", errno));
  }
}

void BasicSocket::ConnectDone() {
  LOG(DEBUG) << "connected to " << *connect_addr_;

  connect_addr_inet_.reset();
  connect_addr_unix_.reset();
  connect_addr_ = nullptr;

  state_ = kOpen;
  watcher_.Call(&Watcher::ConnectionOpen);
}

void BasicSocket::ConnectTimeout() {
  connect_timer_ = event::kNoTimer;
  loop_->WriteFd(socket_);
  ConnectNext(base::make_os_error("connect timed out"));
}

void BasicSocket::ConnectNext(base::error_ptr error) {
  if (connect_timer_ != kNoTimer) {
    loop_->CancelTimer(connect_timer_);
    connect_timer_ = kNoTimer;
  }

  if (socket_ != -1) {
    close(socket_);
    socket_ = -1;
  }

  if (connect_addr_->ai_next) {
    LOG(WARNING)
        << "connecting to " << *connect_addr_ << " failed (" << *error
        << ") - trying next address";
    connect_addr_ = connect_addr_->ai_next;
    Connect();
    return;
  }

  state_ = kFailed;
  watcher_.Call(&Watcher::ConnectionFailed, std::move(error));
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
      ConnectNext(base::make_os_error("getsockopt(SO_ERROR)", errno));
      return;
    } else if (error != 0) {
      ConnectNext(base::make_os_error("connect", error));
      return;
    }

    ConnectDone();
    return;
  }

  watcher_.Call(&Watcher::CanWrite);
}

void BasicSocket::WantRead(bool enabled) {
  CHECK(state_ == kOpen);
  CHECK(!watcher_.empty());

  if (read_requested_ != enabled) {
    if (enabled)
      loop_->ReadFd(socket_, base::borrow(this));
    else
      loop_->ReadFd(socket_);
    read_requested_ = enabled;
  }
}

void BasicSocket::WantWrite(bool enabled) {
  CHECK(state_ == kOpen);
  CHECK(!watcher_.empty());

  if (write_requested_ != enabled) {
    if (enabled)
      loop_->WriteFd(socket_, base::borrow(this));
    else
      loop_->WriteFd(socket_);
    write_requested_ = enabled;
  }
}

base::io_result BasicSocket::Read(void* buf, std::size_t count) {
  CHECK(state_ == kOpen);

  ssize_t ret = read(socket_, buf, count);

  if (ret == -1 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
    return base::io_result::ok(0);

  if (ret == -1)
    return base::io_result::os_error("read", errno);
  if (ret == 0)
    return base::io_result::eof();

  return base::io_result::ok(ret);
}

base::io_result BasicSocket::Write(const void* buf, std::size_t count) {
  CHECK(state_ == kOpen);

  ssize_t ret = write(socket_, buf, count);

  if (ret == -1 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
    return base::io_result::ok(0);  // can't write any data without blocking

  if (ret == -1)
    return base::io_result::os_error("write", errno);

  return base::io_result::ok(ret);
}

/**
 * BoringSSL TLS socket.
 */
class TlsSocket : public Socket, public Socket::Watcher {
 public:
  TlsSocket(const Socket::Builder& opt, BasicSocket::Family family, Socket::Watcher* watcher);
  DISALLOW_COPY(TlsSocket);
  ~TlsSocket();

  base::error_ptr LoadCert(const Socket::Builder& opt);

  void SetWatcher(Socket::Watcher* watcher) override { watcher_.Set(base::borrow(watcher)); }

  void Start() override;
  void WantRead(bool enabled) override;
  void WantWrite(bool enabled) override;
  base::io_result Read(void* buf, std::size_t count) override;
  base::io_result Write(const void* buf, std::size_t count) override;

  bool safe_to_read() const noexcept override {
    return pending_ != kWantReadForWrite && pending_ != kWantWriteForWrite;
  }

  bool safe_to_write() const noexcept override {
    return pending_ != kWantReadForRead && pending_ != kWantWriteForRead;
  }

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

  bool read_requested_ = false;
  bool write_requested_ = false;
  bool read_watched_ = false;
  bool write_watched_ = false;
  PendingOp pending_ = kNone;

  void ConnectionOpen() override;
  void ConnectionFailed(base::error_ptr error) override;
  void CanRead() override;
  void CanWrite() override;

  void WatchRead(bool watch);
  void WatchWrite(bool watch);
};

/** Error type for TLS library errors. */
class tls_error : public base::error {
 public:
  tls_error(const char* what, int tls_code, int ret)
      : what_(what), tls_code_(tls_code), ret_(ret)
  {}

  void format(std::string* str) const override;
  void format(std::ostream* str) const override;

 private:
  const char* what_;
  int tls_code_;
  int ret_;
};

base::error_ptr make_tls_error(const char* what, int tls_code = SSL_ERROR_SSL, int ret = -1) {
  return std::make_unique<tls_error>(what, tls_code, ret);
}

base::io_result tls_error_result(const char* what, int tls_code = SSL_ERROR_SSL, int ret = -1) {
  return base::io_result::error(make_tls_error(what, tls_code, ret));
}

TlsSocket::TlsSocket(const Socket::Builder& opt, BasicSocket::Family family, Socket::Watcher* watcher)
    : socket_(opt, family, this), watcher_(base::borrow(watcher))
{
  ssl_ctx_ = bssl::UniquePtr<SSL_CTX>(SSL_CTX_new(TLS_method()));
  SSL_CTX_set_mode(ssl_ctx_.get(), SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
}

base::error_ptr TlsSocket::LoadCert(const Socket::Builder& opt) {
  ERR_clear_error();
  if (SSL_CTX_use_certificate_chain_file(ssl_ctx_.get(), opt.client_cert_.c_str()) != 1)
    return base::make_file_error(opt.client_cert_, "can't load client certificate");

  const auto& key = opt.client_key_.empty() ? opt.client_cert_ : opt.client_key_;
  ERR_clear_error();
  if (SSL_CTX_use_PrivateKey_file(ssl_ctx_.get(), key.c_str(), SSL_FILETYPE_PEM) != 1)
    return base::make_file_error(key, "can't load private key");

  return nullptr;
}

TlsSocket::~TlsSocket() {}

void TlsSocket::Start() {
  socket_.Start();
}

void TlsSocket::WantRead(bool enabled) {
  CHECK(ssl_);
  CHECK(!watcher_.empty());

  read_requested_ = enabled;
  if (pending_ == kNone && read_watched_ != read_requested_)
    WatchRead(read_requested_);
}

void TlsSocket::WantWrite(bool enabled) {
  CHECK(ssl_);
  CHECK(!watcher_.empty());

  write_requested_ = enabled;
  if (pending_ == kNone && write_watched_ != write_requested_)
    WatchWrite(write_requested_);
}

base::io_result TlsSocket::Read(void* buf, std::size_t count) {
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
      return base::io_result::ok(0);
    } else if (error == SSL_ERROR_WANT_WRITE) {
      if (read_watched_)
        WatchRead(false);
      if (!write_watched_)
        WatchWrite(true);
      pending_ = kWantWriteForRead;
      return base::io_result::ok(0);
    } else {
      return tls_error_result("TLS read", error, ret);
    }
  }

  pending_ = kNone;

  if (read_watched_ != read_requested_)
    WatchRead(read_requested_);
  if (write_watched_ != write_requested_)
    WatchWrite(write_requested_);

  return base::io_result::ok(ret);
}

base::io_result TlsSocket::Write(const void* buf, std::size_t count) {
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
      return base::io_result::ok(0);
    } else if (error == SSL_ERROR_WANT_WRITE) {
      if (read_watched_)
        WatchRead(false);
      if (!write_watched_)
        WatchWrite(true);
      pending_ = kWantReadForWrite;
      return base::io_result::ok(0);
    } else {
      return tls_error_result("TLS write", error, ret);
    }
  }

  pending_ = kNone;

  if (read_watched_ != read_requested_)
    WatchRead(read_requested_);
  if (write_watched_ != write_requested_)
    WatchWrite(write_requested_);

  return base::io_result::ok(ret);
}

void TlsSocket::ConnectionOpen() {
  ssl_ = bssl::UniquePtr<SSL>(SSL_new(ssl_ctx_.get()));
  SSL_set_connect_state(ssl_.get());
  SSL_set_fd(ssl_.get(), socket_.fd());

  watcher_.Call(&Socket::Watcher::ConnectionOpen);
}

void TlsSocket::ConnectionFailed(base::error_ptr error) {
  watcher_.Call(&Socket::Watcher::ConnectionFailed, std::move(error));
}

void TlsSocket::CanRead() {
  CHECK(pending_ != kWantWriteForRead && pending_ != kWantWriteForWrite);
  CHECK(pending_ != kNone || read_requested_);

  if (pending_ == kNone || pending_ == kWantReadForRead)
    watcher_.Call(&Socket::Watcher::CanRead);
  else if (pending_ == kWantReadForWrite)
    watcher_.Call(&Socket::Watcher::CanWrite);
  else
    FATAL("impossible: pending_ invalid");
}

void TlsSocket::CanWrite() {
  CHECK(pending_ != kWantReadForRead && pending_ != kWantReadForWrite);
  CHECK(pending_ != kNone || write_requested_);

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
  socket_.WantRead(watch);
}

void TlsSocket::WatchWrite(bool watch) {
  CHECK(write_watched_ != watch);
  write_watched_ = watch;
  socket_.WantWrite(watch);
}

extern "C" int event_tlsexception_describe_callback(const char* str, std::size_t len, void* ctx_p) {
  auto ctx = static_cast<std::pair<bool, std::string**>*>(ctx_p);

  if (ctx->first) {
    **ctx->second += ", ";
  } else {
    ctx->first = true;
    **ctx->second += " [";
  }

  **ctx->second += str;

  return 1;
}

void tls_error::format(std::string* str) const {
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

  *str += what_;

  *str += ": ";
  if (tls_code_ == SSL_ERROR_SYSCALL && ret_ == 0)
    *str += "EOF";
  else if (tls_code_ == SSL_ERROR_SYSCALL)
    base::os_error(errno).format(str);
  else if (tls_code_ >= 0 && (unsigned)tls_code_ < messages.size())
    *str += messages[tls_code_];
  else
    *str += "unknown TLS error";

  auto ctx = std::make_pair<bool, std::string**>(false, &str);
  ERR_print_errors_cb(&event_tlsexception_describe_callback, &ctx);
  if (ctx.first)
    *str += ']';
}

void tls_error::format(std::ostream* str) const {
  std::string buffer; format(&buffer); *str << buffer;
}

class BasicServerSocket : public ServerSocket, public FdReader {
 public:
  static base::maybe_ptr<ServerSocket> Create(
      Loop* loop,
      Watcher* watcher,
      int domain, int type, int proto,
      struct sockaddr* bind_addr, socklen_t bind_addr_len);

  BasicServerSocket(Loop* loop, Watcher* watcher, int socket);

  ~BasicServerSocket();

  void CanRead(int fd) override;

 private:
  Loop* loop_;
  base::CallbackPtr<Watcher> watcher_;
  int socket_;
};

base::maybe_ptr<ServerSocket> BasicServerSocket::Create(
    Loop* loop,
    Watcher* watcher,
    int domain, int type, int proto,
    struct sockaddr* bind_addr, socklen_t bind_addr_len)
{
  int s = socket(domain, type, proto);
  if (s == -1)
    return base::maybe_os_error<ServerSocket>("socket", errno);
  if (fcntl(s, F_SETFL, O_NONBLOCK) == -1) {
    close(s);
    return base::maybe_os_error<ServerSocket>("fcntl(O_NONBLOCK)", errno);
  }
  if (bind(s, bind_addr, bind_addr_len) == -1) {
    close(s);
    return base::maybe_os_error<ServerSocket>("bind", errno);
  }
  if (listen(s, SOMAXCONN) == -1) {
    close(s);
    return base::maybe_os_error<ServerSocket>("listen", errno);
  }
  return base::maybe_ok<BasicServerSocket>(loop, watcher, s);
}

BasicServerSocket::BasicServerSocket(Loop* loop, Watcher* watcher, int socket)
    : loop_(loop), watcher_(base::borrow(watcher)), socket_(socket)
{
  loop->ReadFd(socket_, base::borrow(this));
}

BasicServerSocket::~BasicServerSocket() {
  loop_->ReadFd(socket_);
  close(socket_);
}

void BasicServerSocket::CanRead(int fd) {
  CHECK(fd == socket_);
  int ret = accept(fd, nullptr, nullptr);

  if (ret == -1 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
    return;  // try again in the next loop
  if (ret == -1) {
    watcher_.Call(&Watcher::AcceptError, base::make_os_error("accept", errno));
    return;
  }

  auto new_socket = std::make_unique<BasicSocket>(loop_, ret);
  watcher_.Call(&Watcher::Accepted, std::move(new_socket));
}

} // namespace internal

base::maybe_ptr<Socket> Socket::Builder::Build(Socket::Watcher* watcher) const {
  if (!watcher)
    watcher = watcher_;

  CHECK(loop_);
  CHECK(watcher);
  CHECK(!tls_ || kind_ == Socket::STREAM);

  internal::BasicSocket::Family family;
  if (!host_.empty() && !port_.empty()) {
    family = INET;
  } else if (!unix_.empty()) {
    if (unix_.length() + 1 > sizeof ((struct sockaddr_un*)nullptr)->sun_path)
      return base::maybe_file_error<Socket>(unix_, "unix socket name too long");
    family = UNIX;
  } else {
    return base::maybe_error<Socket>("{host, port} or unix not specified in Socket::Builder");
  }

  if (tls_) {
    auto socket = std::make_unique<internal::TlsSocket>(*this, family, watcher);
    if (!client_cert_.empty()) {
      auto cert_error = socket->LoadCert(*this);
      if (cert_error)
        return base::maybe_error<Socket>(std::move(cert_error));
    }
    return base::maybe_ok_from<Socket>(std::move(socket));
  }

  return base::maybe_ok<internal::BasicSocket>(*this, family, watcher);
}

base::maybe_ptr<ServerSocket> ListenInet(Loop* loop, ServerSocket::Watcher* watcher, int port) {
  return base::maybe_error<ServerSocket>("TODO: ListenInet not implemented");
}

base::maybe_ptr<ServerSocket> ListenUnix(Loop* loop, ServerSocket::Watcher* watcher, const std::string& path, Socket::Kind kind) {
  struct sockaddr_un addr;

  if (path.length() + 1 > sizeof (addr.sun_path))
    return base::maybe_file_error<ServerSocket>(path, "unix socket name too long");

  unlink(path.c_str());

  addr.sun_family = AF_UNIX;
  std::strcpy(addr.sun_path, path.c_str());

  return internal::BasicServerSocket::Create(
      loop, watcher,
      AF_UNIX, kind, 0,
      (struct sockaddr*) &addr, sizeof addr);
}

} // namespace event
