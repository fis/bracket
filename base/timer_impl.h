/** \file
 * Implementation for handling a set of timers backed by a single `timerfd`.
 *
 * This file should be included only by the `.cc` file in your project
 * that adapts the low-level base/timer.h interface. If you're using
 * event::Loop, that should be the only user of this file.
 */

#include <cerrno>

#include "base/exc.h"
#include "base/log.h"
#include "base/timer.h"

extern "C" {
#include <sys/timerfd.h>
#include <unistd.h>
}

namespace base {

namespace internal {

/**
 * Default implementation of the TimerFd interface.
 *
 * The implementation is based on the Linux `timerfd_create(2)` system call, using the
 * `CLOCK_MONOTONIC` clock.
 */
class DefaultTimerFd : public TimerFd {
 public:
  DefaultTimerFd();
  ~DefaultTimerFd() override { close(fd_); }
  TimerPoint now() const noexcept override { return TimerClock::now(); }
  int fd() const noexcept override { return fd_; }
  void Arm(TimerDuration delay) override;
  void Wait() override;

 private:
  int fd_;
};

DefaultTimerFd::DefaultTimerFd() {
  fd_ = timerfd_create(CLOCK_MONOTONIC, 0);
  if (fd_ == -1)
    throw Exception("timerfd_create", errno);
}

void DefaultTimerFd::Arm(TimerDuration delay) {
  struct itimerspec next_timer = {{0, 0}, {0, 0}};
  next_timer.it_value.tv_nsec = std::chrono::duration_cast<std::chrono::nanoseconds>(delay).count();
  next_timer.it_value.tv_sec = next_timer.it_value.tv_nsec / 1000000000;
  next_timer.it_value.tv_nsec %= 1000000000;

  if (timerfd_settime(fd_, 0, &next_timer, nullptr) == -1)
    throw Exception("timerfd_settime", errno);
}

void DefaultTimerFd::Wait() {
  unsigned char buf[8];

  if (read(fd_, buf, sizeof buf) != sizeof buf) {
    if (errno == EINTR)
      return;
    throw Exception("read(timerfd)", errno);
  }
}

} // namespace internal

/** Base class for objects holding information about timer requests. */
template <typename PeriodicT, typename OneshotT>
struct Timer<PeriodicT, OneshotT>::Request {
  TimerPoint target;  ///< Time when this timer next elapses.
  bool periodic;      ///< `true` if the timer is periodic.
  explicit Request(TimerPoint target, bool periodic) : target(target), periodic(periodic) {}
  virtual ~Request() = default;
  DISALLOW_COPY(Request);
};

/** Periodic timer request. */
template <typename PeriodicT, typename OneshotT>
struct Timer<PeriodicT, OneshotT>::PeriodicRequest : public Timer<PeriodicT, OneshotT>::Request {
  TimerDuration rate;  ///< Repeat rate (period) of this timer.
  PeriodicT data;      ///< Data associated with this timer.

  template <typename... Args>
  explicit PeriodicRequest(TimerPoint target, TimerDuration rate, Args&&... args)
      : Request(target, true), rate(rate), data(std::forward<Args>(args)...) {}
};

/** One-shot timer request. */
template <typename PeriodicT, typename OneshotT>
struct Timer<PeriodicT, OneshotT>::OneshotRequest : public Timer<PeriodicT, OneshotT>::Request {
  OneshotT data;  ///< Data associated with this timer.

  template <typename... Args>
  explicit OneshotRequest(TimerPoint target, Args&&... args)
      : Request(target, false), data(std::forward<Args>(args)...) {}
};

template <typename PeriodicT, typename OneshotT>
Timer<PeriodicT, OneshotT>::Timer(std::unique_ptr<TimerFd> timerfd) {
  if (timerfd)
    timerfd_ = std::move(timerfd);
  else
    timerfd_ = std::make_unique<internal::DefaultTimerFd>();
}

template <typename PeriodicT, typename OneshotT>
Timer<PeriodicT, OneshotT>::~Timer() = default;

template <typename PeriodicT, typename OneshotT>
template <typename... Args>
std::pair<typename Timer<PeriodicT, OneshotT>::Request*, PeriodicT*> Timer<PeriodicT, OneshotT>::AddPeriodic(TimerDuration rate, Args&&... args) {
  if (auto req = periodic_.find(rate); req != periodic_.end())
    return std::pair(req->second, &req->second->data);

  PeriodicRequest* new_req = requests_.insert(std::make_unique<PeriodicRequest>(NextPeriod(rate), rate, std::forward<Args>(args)...));
  periodic_.emplace(rate, new_req);
  queue_.insert(new_req);
  UpdateTimer();

  return std::pair(new_req, &new_req->data);
}

template <typename PeriodicT, typename OneshotT>
template <typename... Args>
std::pair<typename Timer<PeriodicT, OneshotT>::Request*, OneshotT*> Timer<PeriodicT, OneshotT>::AddDelay(TimerDuration delay, Args&&... args) {
  OneshotRequest* req = requests_.insert(std::make_unique<OneshotRequest>(timerfd_->now() + delay, std::forward<Args>(args)...));
  queue_.insert(req);
  UpdateTimer();

  return std::pair(req, &req->data);
}

template <typename PeriodicT, typename OneshotT>
bool Timer<PeriodicT, OneshotT>::Cancel(Request* req) {
  auto owned_req = requests_.claim(req);

  if (!owned_req)
    return false;

  queue_.erase(owned_req.get());
  if (owned_req->periodic)
    periodic_.erase(static_cast<PeriodicRequest*>(owned_req.get())->rate);

  UpdateTimer();
  return true;
}

template <typename PeriodicT, typename OneshotT>
template <typename F>
void Timer<PeriodicT, OneshotT>::Poll(F f) {
  timerfd_->Wait();

  TimerPoint now = timerfd_->now();
  while (!queue_.empty()) {
    Request* req = *queue_.begin();
    if (req->target > now)
      break;

    queue_.erase(queue_.begin());

    if (req->periodic)
      f(&static_cast<PeriodicRequest*>(req)->data, nullptr);
    else
      f(nullptr, &static_cast<OneshotRequest*>(req)->data);

    if (req->periodic) {
      req->target = NextPeriod(static_cast<PeriodicRequest*>(req)->rate);
      queue_.insert(req);
    } else {
      requests_.erase(req);
    }
  }

  UpdateTimer();
}

template <typename PeriodicT, typename OneshotT>
bool Timer<PeriodicT, OneshotT>::RequestOrder::operator()(const Request* a, const Request* b) const noexcept {
  if (a->target != b->target)
    return a->target < b->target;
  return std::less<const Request*>()(a, b);
}

template <typename PeriodicT, typename OneshotT>
TimerPoint Timer<PeriodicT, OneshotT>::NextPeriod(TimerDuration rate) {
  auto realtime_now = std::chrono::system_clock::now();
  auto count_now = realtime_now.time_since_epoch().count();
  auto count_rate = std::chrono::duration_cast<std::chrono::system_clock::duration>(rate).count();
  auto count_delay = count_rate - ((count_now % count_rate) + count_rate) % count_rate;
  return timerfd_->now() + std::chrono::system_clock::duration(count_delay);
}

template <typename PeriodicT, typename OneshotT>
void Timer<PeriodicT, OneshotT>::UpdateTimer() {
  using namespace std::chrono_literals;
  constexpr TimerDuration kSlack = 1ms;

  if (queue_.empty()) // TODO: disarm timer if armed? maybe keep track.
    return;

  TimerDuration delay = ((*queue_.begin())->target - timerfd_->now()) + kSlack;
  if (delay < kSlack)
    delay = kSlack;

  timerfd_->Arm(delay);
}

} // namespace base

// Local Variables:
// mode: c++
// End:
