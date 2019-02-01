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
#include <prometheus/exposer.h>
#include <prometheus/registry.h>

#include "event/loop.h"
#include "irc/connection.h"
#include "irc/message.h"
#include "irc/bot/plugin.h"
#include "proto/util.h"

namespace irc::bot {

namespace internal {

class BotCore : public PluginHost, public irc::Connection::Reader {
 public:
  explicit BotCore(event::Loop* loop);

  template <typename F>
  void RegisterPlugin(const std::string& type, F factory) {
    plugin_registry_.try_emplace(type, factory);
  }

  int Run(const google::protobuf::Message& config);

  void RawReceived(const irc::Message& msg) override;

  void Send(const Message& msg) override;
  event::Loop* loop() override { return loop_; }
  prometheus::Registry* metric_registry() override { return metric_registry_.get(); }

 private:
  using PluginFactory = std::function<std::unique_ptr<Plugin>(const google::protobuf::Message& config, PluginHost* host)>;
  std::unordered_map<std::string, PluginFactory> plugin_registry_;

  event::Loop* loop_;
  std::unique_ptr<event::Loop> private_loop_;

  std::unique_ptr<prometheus::Exposer> metric_exposer_;
  std::shared_ptr<prometheus::Registry> metric_registry_;

  std::vector<std::unique_ptr<Plugin>> plugins_;
  std::unique_ptr<irc::Connection> irc_;
};

} // namespace internal

class Bot {
 public:
  explicit Bot(event::Loop* loop = nullptr) : core_(loop) {}

  template <typename PluginConfigProto, typename PluginClass>
  void RegisterPlugin() {
    core_.RegisterPlugin(
        PluginConfigProto::descriptor()->full_name(),
        [](const google::protobuf::Message& config, PluginHost* host) -> std::unique_ptr<Plugin> {
          return std::make_unique<PluginClass>(static_cast<const PluginConfigProto&>(config), host);
        });
  }

  template <typename ConfigProto>
  int Main(int argc, char** argv, event::Loop* loop = nullptr) {
    if (argc != 2) {
      LOG(ERROR) << "usage: " << argv[0] << " <bot.config>";
      return 1;
    }

    ConfigProto config;
    proto::ReadText(argv[1], &config);
    return core_.Run(config);
  }

 private:
  internal::BotCore core_;
};

} // namespace irc::bot

#endif // IRC_BOT_BOT_H_

// Local Variables:
// mode: c++
// End:
