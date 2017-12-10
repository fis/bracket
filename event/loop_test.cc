#include <chrono>
#include <list>

#include "event/loop.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace event {

using ::testing::_;

std::unordered_set<int> read_fds;
std::unordered_set<int> write_fds;
std::vector<struct pollfd> last_poll;

int fake_poll(struct pollfd* fds, nfds_t nfds, int timeout) {
  last_poll = std::vector<struct pollfd>(fds, fds + nfds);
  std::sort(
      last_poll.begin(), last_poll.end(),
      [](auto a, auto b) { return a.fd < b.fd; });

  int nchanged = 0;

  for (nfds_t i = 0; i < nfds; i++) {
    struct pollfd* pfd = &fds[i];
    bool changed = false;

    pfd->revents = 0;

    if (pfd->fd < 0) continue;

    if ((pfd->events & POLLIN) && read_fds.count(pfd->fd)) {
      pfd->revents |= POLLIN;
      changed = true;
    }

    if ((pfd->events & POLLOUT) && write_fds.count(pfd->fd)) {
      pfd->revents |= POLLOUT;
      changed = true;
    }

    if (changed) ++nchanged;
  }

  return nchanged;
}

constexpr int kFakeTimerFd = 1000;

base::TimerPoint fake_now = base::TimerPoint(base::TimerDuration(5544332211));

class FakeTimerFd : public base::TimerFd {
 public:
  base::TimerPoint now() const noexcept override { return fake_now; }
  int fd() const noexcept override { return kFakeTimerFd; }
  void Arm(base::TimerDuration delay) override {};
  void Wait() override {};
};

struct MockReader : public FdReader {
  MOCK_METHOD1(CanRead, void(int fd));
};

struct MockWriter : public FdWriter {
  MOCK_METHOD1(CanWrite, void(int fd));
};

struct MockTimed : public Timed {
  MOCK_METHOD1(TimerExpired, void(bool periodic));
};

struct LoopTest : public ::testing::Test {
  LoopTest() : loop(&fake_poll, std::make_unique<FakeTimerFd>()) {
    read_fds.clear();
    write_fds.clear();
    last_poll.clear();
  }
  Loop loop;
};

struct BasicPollTest : public LoopTest {
  BasicPollTest() {
    loop.ReadFd(1, &reader);
    loop.WriteFd(2, &writer);
    loop.ReadFd(3, &reader);
    loop.WriteFd(3, &writer);
  }
  MockReader reader;
  MockWriter writer;
};

TEST_F(BasicPollTest, NoChanges) {
  EXPECT_CALL(reader, CanRead(_)).Times(0);
  EXPECT_CALL(writer, CanWrite(_)).Times(0);
  loop.Poll();
}

TEST_F(BasicPollTest, ReadOne) {
  EXPECT_CALL(reader, CanRead(1));
  EXPECT_CALL(writer, CanWrite(_)).Times(0);
  read_fds = {1};
  loop.Poll();
}

TEST_F(BasicPollTest, ReadAll) {
  EXPECT_CALL(reader, CanRead(1));
  EXPECT_CALL(reader, CanRead(3));
  EXPECT_CALL(writer, CanWrite(_)).Times(0);
  read_fds = {1, 2, 3};
  loop.Poll();
}

TEST_F(BasicPollTest, WriteOne) {
  EXPECT_CALL(reader, CanRead(_)).Times(0);
  EXPECT_CALL(writer, CanWrite(2));
  write_fds = {2};
  loop.Poll();
}

TEST_F(BasicPollTest, WriteAll) {
  EXPECT_CALL(reader, CanRead(_)).Times(0);
  EXPECT_CALL(writer, CanWrite(2));
  EXPECT_CALL(writer, CanWrite(3));
  write_fds = {1, 2, 3};
  loop.Poll();
}

TEST_F(BasicPollTest, ReadWrite) {
  EXPECT_CALL(reader, CanRead(1));
  EXPECT_CALL(reader, CanRead(3));
  EXPECT_CALL(writer, CanWrite(2));
  EXPECT_CALL(writer, CanWrite(3));
  read_fds = {1, 2, 3};
  write_fds = {1, 2, 3};
  loop.Poll();
}

TEST_F(LoopTest, CallbackFunction) {
  int read_fd = 0, write_fd = 0;

  loop.ReadFd(1, [&read_fd](int fd) { read_fd = fd; });
  loop.WriteFd(2, [&write_fd](int fd) { write_fd = fd; });

  read_fds = {1};
  write_fds = {2};
  loop.Poll();

  EXPECT_EQ(1, read_fd);
  EXPECT_EQ(2, write_fd);
}

TEST_F(LoopTest, TimerTriggering) {
  using namespace std::chrono_literals;

  unsigned calls = 0;

  loop.Delay(50ns, [&calls](bool) { ++calls; });

  fake_now += 40ns;
  read_fds = {kFakeTimerFd};
  loop.Poll();

  EXPECT_EQ(0, calls);

  fake_now += 10ns;
  read_fds = {kFakeTimerFd};
  loop.Poll();

  EXPECT_EQ(1, calls);

  fake_now += 1000ns;
  read_fds = {kFakeTimerFd};
  loop.Poll();

  EXPECT_EQ(1, calls);
}

} // namespace event
