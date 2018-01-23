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
#include "proto/util.h"

namespace irc::bot {

class Bot : public PluginHost, public irc::Connection::Reader {
 public:
  void Run();

  void Send(const Message& msg) override;
  void MessageReceived(const irc::Message& msg) override;

  event::Loop* loop() override { return loop_; }

  virtual ~Bot() = default;

 protected:
  explicit Bot(event::Loop* loop = nullptr);

  template <typename PluginConfigProto, typename PluginClass>
  void RegisterPlugin() {
    plugin_registry_.try_emplace(
        PluginConfigProto::descriptor()->full_name(),
        [](const google::protobuf::Message& config, PluginHost* host) -> std::unique_ptr<Plugin> {
          return std::make_unique<PluginClass>(static_cast<const PluginConfigProto&>(config), host);
        });
  }

  virtual std::unique_ptr<google::protobuf::Message> LoadConfig() = 0;

 private:
  using PluginFactory = std::function<std::unique_ptr<Plugin>(const google::protobuf::Message& config, PluginHost* host)>;
  std::unordered_map<std::string, PluginFactory> plugin_registry_;

  event::Loop* loop_;
  std::unique_ptr<event::Loop> private_loop_;

  std::vector<std::unique_ptr<Plugin>> plugins_;
  std::unique_ptr<irc::Connection> irc_;
};

template <typename ConfigProto>
class ConfiguredBot : public Bot {
 public:
  ConfiguredBot(const char* config_file) : config_file_(config_file) {}

 protected:
  std::unique_ptr<google::protobuf::Message> LoadConfig() override {
    auto config = std::make_unique<ConfigProto>();
    proto::ReadText(config_file_, config.get());
    return config;
  }

 private:
  const char* config_file_;
};

} // namespace irc::bot

#endif // IRC_BOT_BOT_H_

// Local Variables:
// mode: c++
// End:
