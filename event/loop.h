/** \file
 * Asynchronous event loop.
 */

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

/** Interface for observing file descriptors ready to read. */
struct FdReader : public virtual base::Callback {
  /** Called when \p fd can be read from without blocking. */
  virtual void CanRead(int fd) = 0;
};

/**
 * Callback member function pointer adapter for FdReader.
 *
 * \tparam T object type the callback member function belongs to
 * \tparam method member function pointer to the callback
 */
template <typename T, void (T::*method)(int)>
struct FdReaderM : public FdReader {
  /** Object whose method will be called. */
  T* parent;
  /** Constructs a callback for object \p p, which must outlive this object. */
  FdReaderM(T* p) : parent(p) {}
  /** Implements FdReader::CanRead by calling the callback. */
  void CanRead(int fd) override { (parent->*method)(fd); }
};

/** Interface for observing file descriptors ready to write. */
struct FdWriter : public virtual base::Callback {
  /** Called when \p fd can be written to without blocking. */
  virtual void CanWrite(int fd) = 0;
};

/** Interface for registering timers. */
struct Timed : public virtual base::Callback {
  /** Called when the timer elapses. */
  virtual void TimerExpired(bool periodic) = 0;
};

/**
 * Callback member function pointer adapter for Timed.
 *
 * \tparam T object type the callback member function belongs to
 * \tparam method member function pointer to the callback
 */
template <typename T, void (T::*method)()>
struct TimedM : public Timed {
  /** Object whose method will be called. */
  T* parent;
  /** Constructs a callback for object \p p, which must outlive this object. */
  TimedM(T* p) : parent(p) {}
  /** Implements Timed::TimerExpired by calling the callback. */
  void TimerExpired(bool) override { (parent->*method)(); }
};

/** Specialization of base::Timer to use for timers. */
using Timer = base::Timer<base::CallbackSet<Timed>, base::CallbackPtr<Timed>>;

/** Type for registered timer identifiers. */
using TimerId = base::TimerId<base::CallbackSet<Timed>, base::CallbackPtr<Timed>>;
/** Sentinel value that refers to no timer. */
constexpr TimerId kNoTimer = nullptr;

/** Alias for base::TimerClock. */
using TimerClock = base::TimerClock;
/** Alias for base::TimerPoint. */
using TimerPoint = base::TimerPoint;
/** Alias for base::TimerDuration. */
using TimerDuration = base::TimerDuration;

/**
 * Interface for reacting to events triggered from another thread.
 *
 * \sa Loop::AddClient, ClientLong, ClientPtr
 */
struct Client : public virtual base::Callback {
  /** Payload type that can be passed through a client event. */
  union Data {
    long n;  /**< Integer-valued payload. */
    void* p; /**< Pointer-valued payload. */
  };
  /** Called when a client event has been triggered. */
  virtual void Event(Data data) = 0;
};

/** Type for registered client event identifiers. */
using ClientId = std::uint64_t;

/** Asynchronous event loop. */
class Loop {
 public:
  /** Function pointer type for the implementation of `poll(2)`. */
  using PollFunc = int(struct pollfd*, nfds_t, int);

  /**
   * Constructs a new event loop structure.
   *
   * The \p poll and \p timerfd arguments can be used to override the poll and timer implementations
   * used by the event loop. This is intended to be done only in tests. If not provided, the default
   * implementations use `poll(2)` and `timerfd_create(2)` and the associated functions.
   */
  Loop(PollFunc* poll = &::poll, std::unique_ptr<base::TimerFd> timerfd = std::unique_ptr<base::TimerFd>());
  DISALLOW_COPY(Loop);

  /**
   * Starts or stops observing \p fd for reading.
   *
   * If \p callback is not provided, \p fd will be removed from the set of polled
   * descriptors. Otherwise, it will be added. It is an error to try to add a file descriptor
   * already monitored, or to remove a file descriptor that's not monitored.
   *
   * If \p owned is set, the loop takes ownership of the callback object, and will destroy it when
   * the file descriptor is removed (or the loop is destroyed).
   */
  void ReadFd(int fd, FdReader* callback = nullptr, bool owned = false);
  /** \overload */
  void ReadFd(int fd, const std::function<void(int)>& callback) {
    ReadFd(fd, new FdReaderF(callback), true /* owned */);
  }

  /**
   * Starts or stops observing \p fd for writing.
   *
   * If \p callback is not provided, \p fd will be removed from the set of polled
   * descriptors. Otherwise, it will be added. It is an error to try to add a file descriptor
   * already monitored, or to remove a file descriptor that's not monitored.
   *
   * If \p owned is set, the loop takes ownership of the callback object, and will destroy it when
   * the file descriptor is removed (or the loop is destroyed).
   */
  void WriteFd(int fd, FdWriter* callback = nullptr, bool owned = false);
  /** \overload */
  void WriteFd(int fd, const std::function<void(int)>& callback) {
    WriteFd(fd, new FdWriterF(callback), true /* owned */);
  }

  /**
   * Schedules \p callback to be called after the \p delay has elapsed.
   *
   * This is a one-shot timer: after being called once, the timer is removed automatically.
   *
   * If \p owned is set, the loop takes ownership of the callback object, and will destroy it when
   * the timer elapses, is cancelled, or the loop is destroyed.
   */
  template <typename Duration>
  TimerId Delay(Duration delay, Timed* callback, bool owned = false) {
    return Delay_(std::chrono::duration_cast<base::TimerDuration>(delay), callback, owned);
  }
  /** \overload */
  template <typename Duration>
  TimerId Delay(Duration delay, const std::function<void(bool)>& callback) {
    return Delay(delay, new TimedF(callback), true /* owned */);
  }

  /**
   * Cancels the pending timer \p timer.
   *
   * After this method has been called, the callback is guaranteed to not execute, even if the
   * scheduled time had passed and a call was already pending.
   */
  void CancelTimer(TimerId timer) { timer_.Cancel(timer); }

  /**
   * Adds a listener for custom events from separate threads.
   *
   * If \p owned is set, the loop takes ownership of the callback object, and will destroy it when
   * the listener is removed or the loop is destroyed.
   *
   * \sa Client, ClientLong, ClientPtr
   */
  ClientId AddClient(Client* callback, bool owned = false);

  /** Removes an existing client event registration. */
  bool RemoveClient(ClientId client);

  /**
   * Triggers the callback of a registered client event.
   *
   * Unlike most methods of this class, this method can be safely called from any thread.
   */
  void PostClientEvent(ClientId client, Client::Data data = {0});

  /** Blocks until something happens, and then invokes the relevant callbacks. */
  void Poll();

  /** Returns the current time of the clock used by scheduling timers. */
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

/**
 * Callback member function adapter for Client (with integer data).
 *
 * This class is a convenience wrapper for client events. Constructing it automatically registers a
 * client event on a provided Loop. The object can be called, which (indirectly) causes the
 * specified callback function to be called with the same data.
 *
 * \tparam C object type the callback member function belongs to
 * \tparam method member function pointer to the callback
 * \sa Loop::AddClient, Client, ClientPtr
 */
template <typename C, void (C::*method)(long)>
class ClientLong : public Client {
 public:
  /** Constructs a callback on Loop \p l for object \p cb, both of which must outlive this object. */
  ClientLong(Loop* l, C* cb) : loop_(l), id_(l->AddClient(this)), callback_(cb) {}
  /** Triggers the callback via a client event on the associated Loop. */
  void operator()(long n) { Client::Data d; d.n = n; loop_->PostClientEvent(id_, d); }
  /** Implements Client::Event by calling the callback. */
  void Event(Client::Data data) override { (callback_->*method)(data.n); }
 private:
  Loop* loop_;
  ClientId id_;
  C* callback_;
};

/**
 * Callback member function adapter for Client (with pointer data).
 *
 * This class is a convenience wrapper for client events. Constructing it automatically registers a
 * client event on a provided Loop. The object can be called, which (indirectly) causes the
 * specified callback function to be called with the same data.
 *
 * \tparam C object type the callback member function belongs to
 * \tparam method member function pointer to the callback
 * \sa Loop::AddClient, Client, ClientLong
 */
template <typename T, typename C, void (C::*method)(std::unique_ptr<T>)>
class ClientPtr : public Client {
 public:
  /** Constructs a callback on Loop \p l for object \p cb, both of which must outlive this object. */
  ClientPtr(Loop* l, C* cb) : loop_(l), id_(l->AddClient(this)), callback_(cb) {}
  /** Triggers the callback via a client event on the associated Loop. */
  void operator()(std::unique_ptr<T> p) { Client::Data d; d.p = p.release(); loop_->PostClientEvent(id_, d); }
  /** Implements Client::Event by calling the callback. */
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
