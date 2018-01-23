/** \file
 * IRC bot skeleton.
 */

#ifndef IRC_BOT_BOT_H_
#define IRC_BOT_BOT_H_

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include <google/protobuf/message.h>

#include "event/loop.h"
#include "irc/connection.h"
#include "irc/message.h"
#include "irc/bot/plugin.h"

namespace irc::bot {

class Bot : public PluginHost, public irc::Connection::Reader {
 public:
  Bot(const std::function<std::unique_ptr<google::protobuf::Message>()>& config_loader, event::Loop* loop = nullptr);

  void Run();

  void Send(const Message& msg) override;

  void MessageReceived(const irc::Message& msg) override;

  event::Loop* loop() override { return loop_; }

 private:
  std::function<std::unique_ptr<google::protobuf::Message>()> config_loader_;

  std::vector<void*> module_handles_;
  std::unordered_map<std::string, std::pair<internal::PluginCreator*, internal::PluginDestroyer*>> plugin_registry_;
  std::vector<std::unique_ptr<Plugin, internal::PluginDestroyer*>> plugins_;

  event::Loop* loop_;
  std::unique_ptr<event::Loop> private_loop_;

  std::unique_ptr<irc::Connection> irc_;

  void Load();
};

} // namespace irc::bot

#endif // IRC_BOT_BOT_H_

// Local Variables:
// mode: c++
// End:
