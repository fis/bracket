#include <iostream>
#include <string>

#include "base/log.h"
#include "event/loop.h"
#include "irc/config.pb.h"
#include "irc/connection.h"
#include "irc/message.h"

class Reader : public irc::Connection::Reader {
  void RawReceived(const irc::Message& msg) override {
    std::cerr << "<- ";
    if (!msg.prefix().empty())
      std::cerr << ":" << msg.prefix() << " ";
    std::cerr << msg.command();
    for (const std::string& arg : msg.args())
      std::cerr << " [" << arg << ']';
    std::cerr << '\n';
  }

  virtual void ConnectionReady(const irc::Config::Server& server) override {
    std::cerr << "connection ready: " << server.host() << '\n';
  }

  virtual void ConnectionLost(const irc::Config::Server& server) override {
    std::cerr << "connection lost: " << server.host() << '\n';
  }

  virtual void NickChanged(const std::string& nick) override {
    std::cerr << "new nick: " << nick << '\n';
  }

  virtual void ChannelJoined(const std::string& channel) override {
    std::cerr << "joined: " << channel << '\n';
  }

  virtual void ChannelLeft(const std::string& channel) override {
    std::cerr << "left: " << channel << '\n';
  }
};

int main(int argc, char *argv[]) {
#if 0
  gflags::SetUsageMessage("IRC client library test");
  gflags::ParseCommandLineFlags(&argc, &argv, true);
#endif

  if (argc < 3) {
    std::cerr << "usage: client <host> <port> [tls]\n";
    return 1;
  }

  irc::Config config;
  config.set_nick("test_client");
  config.add_channels("#testchan");
  irc::Config::Server* server = config.add_servers();
  server->set_host(argv[1]);
  server->set_port(argv[2]);
  if (argc >= 4)
    server->mutable_tls();

  event::Loop loop;
  irc::Connection connection(config, &loop);
  connection.AddReader(base::make_owned<Reader>());

  connection.Start();
  while (true)
    loop.Poll();
}
