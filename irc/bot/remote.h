/** \file
 * IRC bot optional remote control RPC interface.
 */

#ifndef IRC_BOT_REMOTE_H_
#define IRC_BOT_REMOTE_H_

#include <functional>

#include "event/loop.h"
#include "irc/bot/module.h"
#include "irc/bot/remote_service.brpc.h"
#include "irc/bot/remote_service.pb.h"

namespace irc::bot {

class Remote : public Module, public RemoteServiceInterface {
 public:
  Remote(const RemoteConfig& config, ModuleHost* host);

  // RemoteServiceInterface
  base::optional_ptr<WatchHandler> Watch(WatchCall* call) override;
  bool SendTo(const ::irc::bot::SendToRequest& req, ::google::protobuf::Empty* resp) override;
  void RemoteServiceError(::base::error_ptr error) override;

  // Module
  void MessageReceived(Connection* conn, const Message& message) override;
  void MessageSent(Connection* conn, const Message& message) override;

 private:
  class ActiveWatcher : public WatchHandler {
   public:
    ActiveWatcher(WatchCall* call, Remote* remote) : call_(call), remote_(remote) {}
    void WatchOpen(WatchCall* call) override {}
    void WatchMessage(WatchCall* call, const ::irc::bot::WatchRequest& req) override;
    void WatchClose(WatchCall* call, ::base::error_ptr error) override;
    void MessageReceived(Connection* conn, const Message& message);
    void MessageSent(Connection* conn, const Message& message);

   private:
    WatchCall* call_;
    Remote* remote_;
    std::vector<std::string> nets_;

    friend class Remote;
  };

  ModuleHost* host_;
  RemoteServiceServer server_;
  base::unique_set<ActiveWatcher> watchers_;
};

/** Enables the support for the remote config module on a bot. */
template <typename Bot>
void RegisterRemoteModule(Bot* bot) {
  bot->template RegisterModule<RemoteConfig, Remote>();
}

} // namespace irc::bot

#endif // IRC_BOT_REMOTE_H_

// Local Variables:
// mode: c++
// End:
