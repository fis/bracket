#include "brpc/testing/echo_service.brpc.h"
#include "gtest/gtest.h"

namespace brpc::testing {

// echo server

class StreamTestService : public EchoServiceInterface::StreamHandler {
  void StreamOpen(EchoServiceInterface::StreamCall* call) override {}

  void StreamMessage(EchoServiceInterface::StreamCall* call, const EchoRequest& req) override {
    EchoResponse resp;
    resp.set_payload(req.payload());
    call->Send(resp);
  }

  void StreamClose(EchoServiceInterface::StreamCall* call, base::error_ptr error) override {
    if (error)
      FAIL() << "Stream call error: " << *error;
  }
};

class TestService : public EchoServiceInterface {
  static StreamTestService kStreamService;

  bool Ping(const EchoRequest& req, EchoResponse* resp) override {
    resp->set_payload(req.payload());
    return true;
  }

  base::optional_ptr<EchoServiceInterface::StreamHandler> Stream(StreamCall*) override {
    return base::borrow(&kStreamService);
  }

  void EchoServiceError(base::error_ptr error) override {
    FAIL() << "RPC service error: " << *error;
  }
};

StreamTestService TestService::kStreamService;
TestService kTestService;

// tests

struct LoopTimeoutTest : public ::testing::Test, public event::Timed {  // TODO: share in //event/testing
  event::Loop loop;
  event::TimerId timeout = event::kNoTimer;

  void RunFor(int seconds) {
    timeout = loop.Delay(std::chrono::seconds(seconds), base::borrow(this));
    loop.Run();
  }

  void TimerExpired(bool) override {
    timeout = event::kNoTimer;
    loop.Stop();
  }

  ~LoopTimeoutTest() { Stop(); }
  void Stop() {
    if (timeout != event::kNoTimer) {
      loop.CancelTimer(timeout);
      timeout = event::kNoTimer;
      loop.Stop();
    }
  }
};

struct PingTest : public LoopTimeoutTest, public EchoServiceClient::PingReceiver {
  bool ok = false;

  void PingDone(const EchoResponse& resp) override {
    EXPECT_EQ(resp.payload(), "hello world");
    ok = true;
    Stop();
  }

  void PingFailed(base::error_ptr error) override {
    FAIL() << "Ping error: " << *error;
    Stop();
  }
};

TEST_F(PingTest, Roundtrip) {
  EchoServiceServer server(&loop, base::borrow(&kTestService));
  auto server_error = server.Start("test.sock");
  if (server_error) FAIL() << *server_error;

  EchoServiceClient client;
  client.target().loop(&loop).unix("test.sock");

  EchoRequest req;
  req.set_payload("hello world");
  client.Ping(req, base::borrow(this));

  RunFor(2);
  EXPECT_TRUE(ok);
}

} // namespace brpc::testing
