#include <iostream>
#include <string>

#include "base/log.h"
#include "event/loop.h"
#include "irc/config.pb.h"
#include "irc/connection.h"
#include "irc/message.h"
#include "proto/util.h"

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
  if (argc < 2) {
    std::cerr << "usage: client <config>\n";
    return 1;
  }

  irc::Config config;
  proto::ReadText(argv[1], &config);

  event::Loop loop;
  irc::Connection connection(config, &loop);
  connection.AddReader(base::make_owned<Reader>());

  connection.Start();
  while (true)
    loop.Poll();
}
