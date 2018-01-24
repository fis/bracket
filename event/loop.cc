#include <cerrno>

#include "base/exc.h"
#include "base/log.h"
#include "event/loop.h"

#include "base/timer_impl.h"

extern "C" {
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

} // namespace internal

Loop::Loop(PollFunc* poll, std::unique_ptr<base::TimerFd> timerfd)
    : poll_(poll),
      timer_(std::move(timerfd)),
      signal_fd_(-1),
      next_client_id_(1),
      client_pipe_{-1, -1},
      stop_(false),
      read_timer_callback_(this),
      read_signal_callback_(this),
      read_client_event_callback_(this),
      handle_sigterm_callback_(this)
{
  ReadFd(timer_.fd(), &read_timer_callback_);

  sigemptyset(&signal_set_);
  AddSignal(SIGTERM, &handle_sigterm_callback_);
  ReadFd(signal_fd_, &read_signal_callback_);
}

void Loop::ReadFd(int fd, FdReader* callback, bool owned) {
  auto&& fd_info = GetFd(fd);
  if (callback) {
    CHECK(fd_info->reader.empty());
    fd_info->reader.Set(callback, owned);
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

void Loop::WriteFd(int fd, FdWriter* callback, bool owned) {
  auto&& fd_info = GetFd(fd);
  if (callback) {
    CHECK(fd_info->writer.empty());
    fd_info->writer.Set(callback, owned);
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

SignalId Loop::AddSignal(int signal, Signal* callback, bool owned) {
  auto other_handler = signal_map_.find(signal);
  bool register_signal = other_handler == signal_map_.end();

  internal::SignalRecord* record = signals_.emplace(callback, owned);
  record->map_item = signal_map_.insert(other_handler, std::make_pair(signal, record));

  if (register_signal) {
    sigaddset(&signal_set_, signal);
    signal_fd_ = signalfd(signal_fd_, &signal_set_, SFD_NONBLOCK | SFD_CLOEXEC);
    if (signal_fd_ == -1)
      throw base::Exception("signalfd(add)", errno);
    int ret = sigprocmask(SIG_BLOCK, &signal_set_, nullptr);
    if (ret == -1)
      throw base::Exception("sigprocmask(SIG_BLOCK)", errno);
  }

  return record;
}

void Loop::RemoveSignal(SignalId signal_id) {
  int signal = signal_id->map_item->first;
  signal_map_.erase(signal_id->map_item);
  signals_.erase(signal_id);

  if (!signal_map_.count(signal)) {
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

ClientId Loop::AddClient(Client* callback, bool owned) {
  ClientId id = next_client_id_++;

  clients_.Add(id, callback, owned);
  if (client_pipe_[0] == -1) {
    if (pipe(client_pipe_) == -1)
      throw base::Exception("pipe(client)", errno);
    ReadFd(client_pipe_[0], &read_client_event_callback_);
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
  CHECK(!fds_.empty()); // TODO: timer support (via timerfd?)

  if (!fds_.empty() && pollfds_.empty()) {
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
}

TimerId Loop::Delay_(base::TimerDuration delay, Timed* callback, bool owned) {
  auto [timer, cb] = timer_.AddDelay(delay);
  cb->Set(callback, owned);
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
    struct signalfd_siginfo sig;

    ssize_t bytes = read(signal_fd_, &sig, sizeof sig);
    if (bytes == -1 && errno == EAGAIN)
      break;
    else if (bytes == -1)
      throw base::Exception("read(signalfd)", errno);
    else if (bytes != sizeof sig)
      throw base::Exception("read(signalfd): short read");

    int signal = sig.ssi_signo;

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
