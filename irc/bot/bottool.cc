#include <iostream>

#include "irc/bot/remote_service.brpc.h"
#include "irc/bot/remote_service.pb.h"

namespace irc::bot::bottool {

static event::Loop loop;

//
// watch
//

struct CmdWatchReceiver : public RemoteServiceClient::WatchReceiver {
  void WatchOpen(RemoteServiceClient::WatchCall* call) override;
  void WatchMessage(RemoteServiceClient::WatchCall* call, const ::irc::bot::IrcEvent& req) override;
  void WatchClose(RemoteServiceClient::WatchCall* call, ::base::error_ptr error) override;
  WatchRequest req;
};

static int CmdWatch(RemoteServiceClient* client, int argc, char** argv) {
  if (argc < 1) {
    std::cerr << "usage: ... watch <net> [<net2> ...]\n";
    return 1;
  }

  auto recv = std::make_unique<CmdWatchReceiver>();
  for (int i = 0; i < argc; i++)
    recv->req.add_nets(argv[i]);

  client->Watch(base::own(std::move(recv)));
  return 0;
}

void CmdWatchReceiver::WatchOpen(RemoteServiceClient::WatchCall* call) {
  call->Send(req);
}

void CmdWatchReceiver::WatchMessage(RemoteServiceClient::WatchCall*, const ::irc::bot::IrcEvent& req) {
  std::cout << req.ShortDebugString() << '\n';
}

void CmdWatchReceiver::WatchClose(RemoteServiceClient::WatchCall*, ::base::error_ptr error) {
  if (error)
    std::cerr << "watch: " << *error << '\n';
  loop.Stop();
}

//
// send
//

struct CmdSendReceiver : public RemoteServiceClient::SendToReceiver {
  void SendToDone(const ::google::protobuf::Empty& resp) override;
  void SendToFailed(::base::error_ptr error) override;
};

static int CmdSend(RemoteServiceClient* client, int argc, char** argv) {
  SendToRequest req;
  IrcEvent* event = req.mutable_event();

  if (argc < 2) {
    std::cerr << "usage: ... send <net> <cmd> [<arg> ...]\n";
    return 1;
  }
  req.set_net(argv[0]);
  event->set_command(argv[1]);
  for (int i = 2; i < argc; i++)
    event->add_args(argv[i]);

  client->SendTo(req, base::make_owned<CmdSendReceiver>());
  return 0;
}

void CmdSendReceiver::SendToDone(const google::protobuf::Empty &) {
  std::cout << "message sent\n";
  loop.Stop();
}

void CmdSendReceiver::SendToFailed(base::error_ptr error) {
  std::cerr << "send: " << *error << '\n';
  loop.Stop();
}

//
// Main
//

using Command = int(RemoteServiceClient*, int, char**);
static std::unordered_map<std::string, Command*> commands = {
  {"watch", CmdWatch},
  {"send", CmdSend},
};

static int Main(int argc, char** argv) {
  if (argc < 3) {
    std::cerr << "usage: " << argv[0] << " <socket> <command> [<arg> ...]\n";
    std::cerr << "commands:";
    for (const auto& entry : commands)
      std::cerr << ' ' << entry.first;
    std::cerr << '\n';
    return 1;
  }

  auto cmd = commands.find(argv[2]);
  if (cmd == commands.end()) {
    std::cerr << "unknown command: " << argv[2] << '\n';
    return 1;
  }

  RemoteServiceClient client;
  client.target().loop(&loop).unix(argv[1]);
  int ret = cmd->second(&client, argc - 3, argv + 3);
  if (ret == 0)
    loop.Run();
  return ret;
}

} // namespace irc::bot::bottool

int main(int argc, char** argv) {
  return irc::bot::bottool::Main(argc, argv);
}
