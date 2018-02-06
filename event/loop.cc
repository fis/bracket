#include <cerrno>

#include "base/exc.h"
#include "base/log.h"
#include "event/loop.h"

#include "base/timer_impl.h"

extern "C" {
#include <poll.h>
#include <signal.h>
#include <sys/signalfd.h>
#include <unistd.h>
}

namespace event {

namespace internal {

/** Client event data sent over the pipe. */
struct ClientEventData {
  ClientId id;        ///< Client event identifier.
  Client::Data data;  ///< Payload data.
  ClientEventData() = default;
  /** Initializes client event data with id \p i and payload \p d. */
  ClientEventData(ClientId i, Client::Data d) : id(i), data(d) {}
};

class SignalFdImpl : public Loop::SignalFd {
 public:
  SignalFdImpl();
  ~SignalFdImpl();
  void Add(int signal) override;
  void Remove(int signal) override;
  int Read() override;
  int fd() const noexcept override { return signal_fd_; }

 private:
  int signal_fd_ = -1;
  sigset_t signal_set_;
};

SignalFdImpl::SignalFdImpl() {
  sigemptyset(&signal_set_);
}

SignalFdImpl::~SignalFdImpl() {
  if (signal_fd_ != -1)
    close(signal_fd_);
}

void SignalFdImpl::Add(int signal) {
  sigaddset(&signal_set_, signal);
  signal_fd_ = signalfd(signal_fd_, &signal_set_, SFD_NONBLOCK | SFD_CLOEXEC);
  if (signal_fd_ == -1)
    throw base::Exception("signalfd(add)", errno);
  int ret = sigprocmask(SIG_BLOCK, &signal_set_, nullptr);
  if (ret == -1)
    throw base::Exception("sigprocmask(SIG_BLOCK)", errno);
}

void SignalFdImpl::Remove(int signal) {
  {
    sigset_t unblock_set;
    sigemptyset(&unblock_set);
    sigaddset(&unblock_set, signal);
    int ret = sigprocmask(SIG_UNBLOCK, &unblock_set, nullptr);
    if (ret == -1)
      throw base::Exception("sigprocmask(SIG_UNBLOCK)", errno);
    sigdelset(&signal_set_, signal);
    signal_fd_ = signalfd(signal_fd_, &signal_set_, SFD_NONBLOCK | SFD_CLOEXEC);
    if (signal_fd_ == -1)
      throw base::Exception("signalfd(remove)", errno);
  }
}

int SignalFdImpl::Read() {
  struct signalfd_siginfo sig;

  ssize_t bytes = read(signal_fd_, &sig, sizeof sig);
  if (bytes == -1 && errno == EAGAIN)
    return -1;
  else if (bytes == -1)
    throw base::Exception("read(signalfd)", errno);
  else if (bytes != sizeof sig)
    throw base::Exception("read(signalfd): short read");

  return sig.ssi_signo;
}

} // namespace internal

Loop::Loop()
    : Loop(&::poll, std::unique_ptr<base::TimerFd>(), std::make_unique<internal::SignalFdImpl>())
{}

Loop::Loop(PollFunc* poll, std::unique_ptr<base::TimerFd> timer, std::unique_ptr<SignalFd> signal_fd)
    : poll_(poll),
      timer_(std::move(timer)),
      signal_fd_(std::move(signal_fd))
{
  ReadFd(timer_.fd(), base::borrow(&read_timer_callback_));

  AddSignal(SIGTERM, base::borrow(&handle_sigterm_callback_));
  ReadFd(signal_fd_->fd(), base::borrow(&read_signal_callback_));
}

void Loop::ReadFd(int fd, base::optional_ptr<FdReader> callback) {
  auto&& fd_info = GetFd(fd);
  if (callback) {
    CHECK(fd_info->reader.empty());
    fd_info->reader.Set(std::move(callback));
    if (!pollfds_.empty()) {
      struct pollfd *pfd = &pollfds_[fd_info->pollfd_index];
      pfd->events |= POLLIN;
      if (pfd->fd < 0) pfd->fd = -pfd->fd;
    }
  } else {
    fd_info->reader.Clear();
    if (fd_info->reader.empty() && fd_info->writer.empty()) {
      fds_.erase(fd);
      pollfds_.clear();
    } else if (!pollfds_.empty()) {
      pollfds_[fd_info->pollfd_index].events &= ~POLLIN;
    }
  }
}

void Loop::WriteFd(int fd, base::optional_ptr<FdWriter> callback) {
  auto&& fd_info = GetFd(fd);
  if (callback) {
    CHECK(fd_info->writer.empty());
    fd_info->writer.Set(std::move(callback));
    if (!pollfds_.empty()) {
      struct pollfd *pfd = &pollfds_[fd_info->pollfd_index];
      pfd->events |= POLLOUT;
      if (pfd->fd < 0) pfd->fd = -pfd->fd;
    }
  } else {
    fd_info->writer.Clear();
    if (fd_info->reader.empty() && fd_info->writer.empty()) {
      fds_.erase(fd);
      pollfds_.clear();
    } else if (!pollfds_.empty()) {
      pollfds_[fd_info->pollfd_index].events &= ~POLLOUT;
    }
  }
}

SignalId Loop::AddSignal(int signal, base::optional_ptr<Signal> callback) {
  auto other_handler = signal_map_.find(signal);
  bool register_signal = other_handler == signal_map_.end();

  internal::SignalRecord* record = signals_.emplace(std::move(callback));
  record->map_item = signal_map_.insert(other_handler, std::make_pair(signal, record));

  if (register_signal)
    signal_fd_->Add(signal);

  return record;
}

void Loop::RemoveSignal(SignalId signal_id) {
  int signal = signal_id->map_item->first;
  signal_map_.erase(signal_id->map_item);
  signals_.erase(signal_id);

  if (!signal_map_.count(signal))
    signal_fd_->Remove(signal);
}

ClientId Loop::AddClient(base::optional_ptr<Client> callback) {
  ClientId id = next_client_id_++;

  clients_.Add(id, std::move(callback));
  if (client_pipe_[0] == -1) {
    if (pipe(client_pipe_) == -1)
      throw base::Exception("pipe(client)", errno);
    ReadFd(client_pipe_[0], base::borrow(&read_client_event_callback_));
  }

  return id;
}

bool Loop::RemoveClient(ClientId client) {
  bool removed = clients_.Remove(client);

  if (removed && clients_.empty()) {
    CHECK(client_pipe_[0] != -1);
    ReadFd(client_pipe_[0]);
    close(client_pipe_[0]);
    close(client_pipe_[1]);
    client_pipe_[0] = -1;
  }

  return removed;
}

void Loop::PostClientEvent(ClientId client, Client::Data data) {
  if (client_pipe_[0] != -1) {
    internal::ClientEventData payload(client, data);
    ssize_t wrote = write(client_pipe_[1], &payload, sizeof payload);
    if (wrote == -1)
      throw base::Exception("write(client)", errno);
    if (wrote != sizeof payload)
      throw base::Exception("write(client): short write");
  }
}

void Loop::Poll() {
  CHECK(!fds_.empty());

  if (pollfds_.empty()) {
    pollfds_.resize(fds_.size());

    std::size_t pollfd_index = 0;
    for (auto&& fd_item : fds_) {
      int fd = fd_item.first;
      Fd* fd_info = &fd_item.second;

      int events = 0;
      if (!fd_info->reader.empty())
        events |= POLLIN;
      if (!fd_info->writer.empty())
        events |= POLLOUT;

      fd_info->pollfd_index = pollfd_index;

      struct pollfd* pfd = &pollfds_[pollfd_index];
      pfd->fd = events ? fd : -fd;
      pfd->events = events;

      ++pollfd_index;
    }
  }

  int changed = poll_(pollfds_.data(), pollfds_.size(), -1);
  if (changed == -1 && errno != EINTR)
    throw base::Exception("poll", errno);

  if (changed > 0) {
    // Calling the callbacks may add or remove descriptors, which
    // could invalidate iterators to fds_ or pollfds_. Collect a
    // separate list of file descriptors on which callbacks need to be
    // invoked, first.

    std::vector<std::pair<int, bool /* write? */>> events;

    // TODO POLLNVAL?
    for (const struct pollfd& pfd : pollfds_) {
      if (pfd.revents & (POLLIN|POLLERR|POLLHUP))
        events.emplace_back(pfd.fd, false);
      if (pfd.revents & (POLLOUT|POLLERR))
        events.emplace_back(pfd.fd, true);
    }

    for (const auto& event : events) {
      int fd = event.first;

      const auto fd_entry = fds_.find(fd);
      if (fd_entry == fds_.end())
        continue; // no longer relevant

      if (event.second)
        fd_entry->second.writer.Call(&FdWriter::CanWrite, fd);
      else
        fd_entry->second.reader.Call(&FdReader::CanRead, fd);
    }
  }

  finishable_.Flush(&Finishable::LoopFinished);
}

TimerId Loop::Delay_(base::TimerDuration delay, base::optional_ptr<Timed> callback) {
  auto [timer, cb] = timer_.AddDelay(delay);
  cb->Set(std::move(callback));
  return timer;
}

Loop::Fd* Loop::GetFd(int fd) {
  auto [fd_data, inserted] = fds_.try_emplace(fd);

  if (inserted)
    pollfds_.clear();

  return &fd_data->second;
}

void Loop::ReadTimer(int) {
  timer_.Poll([&](base::CallbackSet<Timed>* periodic, base::CallbackPtr<Timed>* oneshot) {
      CHECK(periodic || oneshot);
      if (periodic)
        periodic->Call(&Timed::TimerExpired, true);
      else
        oneshot->Call(&Timed::TimerExpired, false);
    });
}

void Loop::ReadSignal(int) {
  while (true) {
    int signal = signal_fd_->Read();
    if (signal == -1)
      break;

    auto handlers = signal_map_.equal_range(signal);
    if (handlers.first == signal_map_.end()) {
      LOG(WARNING) << "signalfd produced an unexpected signal: " << signal;
      continue;
    }

    // TODO: prune the signal if all the handlers were empty
    for (auto it = handlers.first; it != handlers.second; ++it) {
      it->second->callback.Call(&Signal::SignalDelivered, signal);
    }
  }
}

void Loop::ReadClientEvent(int) {
  internal::ClientEventData payload;

  ssize_t got = read(client_pipe_[0], &payload, sizeof payload);
  if (got == -1)
    throw base::Exception("read(client)", errno);
  if (got != sizeof payload)
    throw base::Exception("read(client): short read");

  clients_.Call(payload.id, &Client::Event, payload.data);
}

} // namespace event

template class base::Timer<base::CallbackSet<event::Timed>, base::CallbackPtr<event::Timed>>;
