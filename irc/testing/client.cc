#include <iostream>
#include <string>

#include "base/log.h"
#include "event/loop.h"
#include "irc/config.pb.h"
#include "irc/connection.h"
#include "irc/message.h"

class Reader : public irc::Connection::Reader {
  void MessageReceived(const irc::Message& msg) override {
    std::cerr << "Got this:\n";
    if (!msg.prefix().empty())
      std::cerr << "  prefix: " << msg.prefix() << '\n';
    std::cerr << "  command: " << msg.command() << '\n';
    for (const std::string& arg : msg.args())
      std::cerr << "  arg: " << arg << '\n';
  }
};

int main(int argc, char *argv[]) {
#if 0
  gflags::SetUsageMessage("IRC client library test");
  gflags::ParseCommandLineFlags(&argc, &argv, true);
#endif

  if (argc != 3) {
    std::cerr << "usage: client <host> <port>\n";
    return 1;
  }

  irc::Config config;
  irc::Config::Server* server = config.add_servers();
  server->set_host(argv[1]);
  server->set_port(argv[2]);
  server->mutable_tls();

  event::Loop loop;
  irc::Connection connection(config, &loop);
  connection.AddReader(base::make_owned<Reader>());

  connection.Start();
  while (true)
    loop.Poll();
}
