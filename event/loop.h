/** \file
 * Asynchronous event loop.
 */

#ifndef EVENT_LOOP_H_
#define EVENT_LOOP_H_

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <tuple>
#include <utility>
#include <vector>

#include "base/callback.h"
#include "base/common.h"
#include "base/timer.h"

extern "C" {
#include <poll.h>  // nfds_t
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
  explicit FdReaderM(T* p) : parent(p) {}
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
  explicit TimedM(T* p) : parent(p) {}
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

/** Interface for registering cleanup handlers at end of each poll. */
struct Finishable : public virtual base::Callback {
  /** Called after the current/next round of poll event processing, in registration order. */
  virtual void LoopFinished() = 0;
};

/** Interface for registering signals. */
struct Signal : public virtual base::Callback {
  /** Called when a signal is delivered. */
  virtual void SignalDelivered(int signal) = 0;
};

/**
 * Callback member function pointer adapter for Signal.
 *
 * \tparam T object type the callback member function belongs to
 * \tparam method member function pointer to the callback
 */
template <typename T, void (T::*method)(int)>
struct SignalM : public Signal {
  /** Object whose method will be called. */
  T* parent;
  /** Constructs a callback for object \p p, which must outlive this object. */
  explicit SignalM(T* p) : parent(p) {}
  /** Implements Signal::SignalDelivered by calling the callback. */
  void SignalDelivered(int signal) override { (parent->*method)(signal); }
};

namespace internal { struct SignalRecord; }
/** Type for registered signal handler identifiers. */
using SignalId = internal::SignalRecord*;
/** Sentinel value that refers to no signal handler. */
constexpr SignalId kNoSignal = nullptr;

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

namespace internal {

using SignalMap = std::unordered_multimap<int, SignalRecord*>;
struct SignalRecord {
  base::CallbackPtr<event::Signal> callback;
  SignalMap::const_iterator map_item;
  SignalRecord(base::optional_ptr<event::Signal> cb) : callback(std::move(cb)) {}
};

} // namespace internal

/** Asynchronous event loop. */
class Loop {
 public:
  /** Function pointer type for the implementation of `poll(2)`. */
  using PollFunc = int(struct pollfd*, nfds_t, int);
  /** Interface for `signalfd(2)`. */
  struct SignalFd {
    virtual void Add(int signal) = 0;
    virtual void Remove(int signal) = 0;
    virtual int Read() = 0;
    virtual int fd() const noexcept = 0;
    virtual ~SignalFd() = default;
  };

  /** Constructs a new event loop. */
  Loop();
  /**
   * Constructs a new event loop for testing.
   *
   * The \p poll, \p timer and \p signal_fd arguments override the default implementations used by
   * the event loop. This is intended to be done only in tests.
   */
  Loop(PollFunc* poll, std::unique_ptr<base::TimerFd> timer, std::unique_ptr<SignalFd> signal_fd);

  DISALLOW_COPY(Loop);

  /**
   * Starts or stops observing \p fd for reading.
   *
   * If \p callback is not provided, \p fd will be removed from the set of polled
   * descriptors. Otherwise, it will be added. It is an error to try to add a file descriptor
   * already monitored, or to remove a file descriptor that's not monitored.
   *
   * If the callback pointer is owned, the callback will be destroyed when the file descriptor is
   * removed (or the loop is destroyed).
   */
  void ReadFd(int fd, base::optional_ptr<FdReader> callback = nullptr);
  /** \overload */
  void ReadFd(int fd, const std::function<void(int)>& callback) {
    ReadFd(fd, base::make_owned<FdReaderF>(callback));
  }

  /**
   * Starts or stops observing \p fd for writing.
   *
   * If \p callback is not provided, \p fd will be removed from the set of polled
   * descriptors. Otherwise, it will be added. It is an error to try to add a file descriptor
   * already monitored, or to remove a file descriptor that's not monitored.
   *
   * If the callback pointer is owned, the callback will be destroyed when the file descriptor is
   * removed (or the loop is destroyed).
   */
  void WriteFd(int fd, base::optional_ptr<FdWriter> callback = nullptr);
  /** \overload */
  void WriteFd(int fd, const std::function<void(int)>& callback) {
    WriteFd(fd, base::make_owned<FdWriterF>(callback));
  }

  /**
   * Schedules \p callback to be called after the \p delay has elapsed.
   *
   * This is a one-shot timer: after being called once, the timer is removed automatically.
   *
   * If the callback pointer is owned, the callback will be destroyed when the timer elapses, is
   * cancelled, or the loop is destroyed.
   */
  template <typename Duration>
  TimerId Delay(Duration delay, base::optional_ptr<Timed> callback) {
    return Delay_(std::chrono::duration_cast<base::TimerDuration>(delay), std::move(callback));
  }
  /** \overload */
  template <typename Duration>
  TimerId Delay(Duration delay, const std::function<void(bool)>& callback) {
    return Delay(delay, base::make_owned<TimedF>(callback));
  }

  /**
   * Cancels the pending timer \p timer.
   *
   * After this method has been called, the callback is guaranteed to not execute, even if the
   * scheduled time had passed and a call was already pending.
   */
  void CancelTimer(TimerId timer) { timer_.Cancel(timer); }

  /** Registers a finisher handler to run after the current loop. */
  void AddFinishable(base::optional_ptr<Finishable> callback) { finishable_.Add(std::move(callback)); }

  /** Registers a listener for signal \p signal. */
  SignalId AddSignal(int signal, base::optional_ptr<Signal> callback);
  /** Removes a registered signal handler. */
  void RemoveSignal(SignalId signal_id);

  /**
   * Adds a listener for custom events from separate threads.
   *
   * \sa Client, ClientLong, ClientPtr
   */
  ClientId AddClient(base::optional_ptr<Client> callback);

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

  /** Runs until asked to terminate (via the Stop() method). */
  void Run() {
    while (!stop_)
      Poll();
    stop_ = false;
  }

  void Stop() { stop_ = true; }

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

  base::CallbackQueue<Finishable> finishable_;

  std::unique_ptr<SignalFd> signal_fd_;
  internal::SignalMap signal_map_;
  base::unique_set<internal::SignalRecord> signals_;

  base::CallbackMap<ClientId, Client> clients_;
  ClientId next_client_id_ = 1;
  int client_pipe_[2] = {-1, -1};

  bool stop_ = false;  ///< `true` if a stop request is pending

  TimerId Delay_(base::TimerDuration delay, base::optional_ptr<Timed> callback);
  Fd* GetFd(int fd);
  void ReadTimer(int);
  void ReadSignal(int);
  void ReadClientEvent(int);

  void HandleSigTerm(int) { Stop(); }

  FdReaderM<Loop, &Loop::ReadTimer> read_timer_callback_{this};
  FdReaderM<Loop, &Loop::ReadSignal> read_signal_callback_{this};
  FdReaderM<Loop, &Loop::ReadClientEvent> read_client_event_callback_{this};
  SignalM<Loop, &Loop::HandleSigTerm> handle_sigterm_callback_{this};
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
  /** Constructs a callback on Loop \p l for object \p cb. Both must outlive this object. */
  ClientLong(Loop* l, C* cb) : loop_(l), id_(l->AddClient(base::borrow(this))), callback_(cb) {}
  ~ClientLong() { loop_->RemoveClient(id_); }
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
  /** Constructs a callback on Loop \p l for object \p cb. Both must outlive this object. */
  ClientPtr(Loop* l, C* cb) : loop_(l), id_(l->AddClient(base::borrow(this))), callback_(cb) {}
  ~ClientPtr() { loop_->RemoveClient(id_); }
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
