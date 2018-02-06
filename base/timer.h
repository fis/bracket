/** \file
 * Interface for handling a set of timers backed by a single `timerfd`.
 */

#ifndef BASE_TIMER_H_
#define BASE_TIMER_H_

#include <chrono>
#include <map>
#include <set>
#include <utility>

#include "base/common.h"
#include "base/unique_set.h"

namespace base {

/** std::chrono clock used by the timer. */
using TimerClock = std::chrono::steady_clock;
/** std::chrono::time_point of TimerClock. */
using TimerPoint = TimerClock::time_point;
/** std::chrono::duration of TimerClock. */
using TimerDuration = TimerClock::duration;

/** Interface for a `timerfd` instance. */
struct TimerFd {
  virtual ~TimerFd() {}
  /** Arms the timer to wait for \p delay. */
  virtual void Arm(TimerDuration delay) = 0;
  /** Blocks until a timer expires or a spurious wakeup occurs. */
  virtual void Wait() = 0;
  /** Returns the current time. */
  virtual TimerPoint now() const noexcept = 0;
  /** Returns the file descriptor that needs to be observed. */
  virtual int fd() const noexcept = 0;
};

/**
 * Low-level wrapper for handling a set of timers with a single `timerfd`.
 *
 * This class is intentionally low-level. Most clients should probably use this through a
 * wrapper. The scheduling functions of event::Loop tie this to the event loop mechanisms. It's also
 * possible to integrate this class in a GLib main loop.
 *
 * The semantics of this class are pretty simple: it holds a collection of \p PeriodicT objects
 * attached to (unique) intervals, and a collection of \p OneshotT objects attached to delays. The
 * Poll() method waits for at least one timer to expire (unless interrupted), then calls the
 * provided function with all so far expired timers. You should probably arrange for Poll() to be
 * called only when the file descriptor returned by #fd() indicates being ready to read.
 *
 * Periodic tags will be delivered at constant multiples of the rate, so using a rate of (e.g.) 1
 * minute will cause a tag to be delivered right after the start of a new minute.
 *
 * \tparam PeriodicT object type for periodic timers
 * \tparam OneshotT object type for one-shot timers
 */
template <typename PeriodicT, typename OneshotT = PeriodicT>
class Timer {
 public:
  struct Request;

  /**
   * Constructs a timer.
   *
   * If \p timerfd is not provided (or is empty), a default implementation (which uses
   * `timerfd_create`) is constructed.
   */
  Timer(std::unique_ptr<TimerFd> timerfd = std::unique_ptr<TimerFd>());
  DISALLOW_COPY(Timer);
  ~Timer();

  /**
   * Adds a new periodic timer, or returns the existing one.
   *
   * If the set already owns an object for \p rate, a pointer to that object is returned, and the
   * extra arguments are ignored. The object is already scheduled for delivery at that rate.
   *
   * If the set doesn't own a suitable object, the provided arguments are forwarded to a matching \p
   * PeriodicT constructor, and a pointer to that object is returned. It will be owned by the
   * set. The timer will start delivering the new object at the requested rate.
   *
   * The return value is a pair of pointers: one to the opaque `Request` type (which should be
   * considered an arbitrary handle to the timer), and another to the associated data object held by
   * this class.
   *
   * \tparam Args constructor argument types used if a new object is needed
   * \param rate periodic timer rate
   * \param args constructor arguments used if a new object is needed
   */
  template <typename... Args>
  std::pair<Request*, PeriodicT*> AddPeriodic(TimerDuration rate, Args&&... args);

  /**
   * Adds a new one-shot timer.
   *
   * The class will own the associated data, and also destroy it automatically after it's been
   * delivered via Poll(). The object will be constructed with the \p OneshotT constructor matching
   * the forwarded arguments.
   *
   * \tparam Args constructor argument types for the new object
   * \param delay wait before delivering the object
   * \param args constructor arguments for the new object
   */
  template <typename... Args>
  std::pair<Request*, OneshotT*> AddDelay(TimerDuration delay, Args&&... args);

  /**
   * Cancels a previously requested timer.
   *
   * This will destroy the associated data. Note that for a periodic timer, there's only one object
   * per unique period.
   */
  bool Cancel(Request* timer);

  /**
   * Delivers the expiring timers, and their attached objects.
   *
   * The type of the callback should be `void(PeriodicT*, OneshotT*)`. Exactly one of the passed-in
   * pointers will be non-`nullptr`, depending on whether the expiring timer is periodic or not.
   *
   * The pointed-to object is owned by the set. If it's a one-shot timer, it will be destroyed
   * automatically after the call.
   *
   * If the file descriptor returned by #fd() isn't ready for reading, this call might block until
   * the next timer expires. If the read gets interrupted by a signal, \p f might not get called at
   * all.
   *
   * \tparam F callable with the signature `void(PeriodicT *, OneshotT *)`
   */
  template <typename F>
  void Poll(F f);

  /** Returns the file descriptor that Poll() will try to read. */
  int fd() const noexcept { return timerfd_->fd(); }

 private:
  struct PeriodicRequest;
  struct OneshotRequest;

  struct RequestOrder {
    bool operator()(const Request* a, const Request* b) const noexcept;
  };

  std::unique_ptr<TimerFd> timerfd_;

  base::unique_set<Request> requests_;
  std::map<TimerDuration, PeriodicRequest*> periodic_;
  std::set<Request*, RequestOrder> queue_;

  TimerPoint NextPeriod(TimerDuration rate);
  void UpdateTimer();
};

template <typename PeriodicT, typename OneshotT = PeriodicT>
using TimerId = typename Timer<PeriodicT, OneshotT>::Request*;

} // namespace base

#endif // BASE_TIMER_H_

// Local Variables:
// mode: c++
// End:
