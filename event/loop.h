#ifndef EVENT_LOOP_H_
#define EVENT_LOOP_H_

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <queue>
#include <tuple>
#include <utility>
#include <vector>

#include "base/callback.h"
#include "base/common.h"
#include "base/timer.h"

extern "C" {
#include <poll.h>
}

namespace event {

struct FdReader : public virtual base::Callback {
  virtual void CanRead(int fd) = 0;
};

template <typename T, void (T::*method)(int)>
struct FdReaderM : public FdReader {
  T* parent;
  FdReaderM(T* p) : parent(p) {}
  void CanRead(int fd) override { (parent->*method)(fd); }
};

struct FdWriter : public virtual base::Callback {
  virtual void CanWrite(int fd) = 0;
};

struct Timed : public virtual base::Callback {
  virtual void TimerExpired(bool periodic) = 0;
};

template <typename T, void (T::*method)()>
struct TimedM : public Timed {
  T* parent;
  TimedM(T* p) : parent(p) {}
  void TimerExpired(bool) override { (parent->*method)(); }
};

using Timer = base::Timer<base::CallbackSet<Timed>, base::CallbackPtr<Timed>>;

using TimerId = base::TimerId<base::CallbackSet<Timed>, base::CallbackPtr<Timed>>;
constexpr TimerId kNoTimer = nullptr;

using TimerClock = base::TimerClock;
using TimerPoint = base::TimerPoint;
using TimerDuration = base::TimerDuration;

struct Client : public virtual base::Callback {
  union Data {
    long n;
    void* p;
  };
  virtual void Event(Data data) = 0;
};

using ClientId = std::uint64_t;

class Loop {
 public:
  using PollFunc = int(struct pollfd*, nfds_t, int);

  Loop(PollFunc* poll = &::poll, std::unique_ptr<base::TimerFd> timerfd = std::unique_ptr<base::TimerFd>());
  DISALLOW_COPY(Loop);

  void ReadFd(int fd, FdReader* callback = nullptr, bool owned = false);
  void ReadFd(int fd, const std::function<void(int)>& callback) {
    ReadFd(fd, new FdReaderF(callback), true /* owned */);
  }

  void WriteFd(int fd, FdWriter* callback = nullptr, bool owned = false);
  void WriteFd(int fd, const std::function<void(int)>& callback) {
    WriteFd(fd, new FdWriterF(callback), true /* owned */);
  }

  template <typename Duration>
  TimerId Delay(Duration delay, Timed* callback, bool owned = false) {
    return Delay_(std::chrono::duration_cast<base::TimerDuration>(delay), callback, owned);
  }
  template <typename Duration>
  TimerId Delay(Duration delay, const std::function<void(bool)>& callback) {
    return Delay(delay, new TimedF(callback), true /* owned */);
  }

  void CancelTimer(TimerId timer) { timer_.Cancel(timer); }

  ClientId AddClient(Client* callback, bool owned = false);

  bool RemoveClient(ClientId client);

  void PostClientEvent(ClientId client, Client::Data data = {0});

  void Poll();

  base::TimerPoint now() const noexcept { return base::TimerClock::now(); /* TODO test now */ }

 private:
  CALLBACK_F1(FdReaderF, FdReader, CanRead, int);
  CALLBACK_F1(FdWriterF, FdWriter, CanWrite, int);
  CALLBACK_F1(TimedF, Timed, TimerExpired, bool);

  struct Fd {
    base::CallbackPtr<FdReader> reader;
    base::CallbackPtr<FdWriter> writer;
    std::size_t pollfd_index;
  };

  PollFunc* poll_;

  std::map<int, Fd> fds_;
  std::vector<struct pollfd> pollfds_;

  Timer timer_;

  base::CallbackMap<ClientId, Client> clients_;
  ClientId next_client_id_;
  int client_pipe_[2];

  TimerId Delay_(base::TimerDuration delay, Timed* callback, bool owned);
  Fd* GetFd(int fd);
  void ReadTimer(int);
  void ReadClientEvent(int);

  FdReaderM<Loop, &Loop::ReadTimer> read_timer_callback_;
  FdReaderM<Loop, &Loop::ReadClientEvent> read_client_event_callback_;
};

template <typename C, void (C::*method)(long)>
class ClientLong : public Client {
 public:
  ClientLong(Loop* l, C* cb) : loop_(l), id_(l->AddClient(this)), callback_(cb) {}
  void operator()(long n) { Client::Data d; d.n = n; loop_->PostClientEvent(id_, d); }
  void Event(Client::Data data) override { (callback_->*method)(data.n); }
 private:
  Loop* loop_;
  ClientId id_;
  C* callback_;
};

template <typename T, typename C, void (C::*method)(std::unique_ptr<T>)>
class ClientPtr : public Client {
 public:
  ClientPtr(Loop* l, C* cb) : loop_(l), id_(l->AddClient(this)), callback_(cb) {}
  void operator()(std::unique_ptr<T> p) { Client::Data d; d.p = p.release(); loop_->PostClientEvent(id_, d); }
  void Event(Client::Data data) override { (callback_->*method)(std::unique_ptr<T>(static_cast<T*>(data.p))); }
 private:
  Loop* loop_;
  ClientId id_;
  C* callback_;
};

} // namespace event

extern template class base::Timer<base::CallbackSet<event::Timed>, base::CallbackPtr<event::Timed>>;

#endif // EVENT_LOOP_H_

// Local Variables:
// mode: c++
// End:
