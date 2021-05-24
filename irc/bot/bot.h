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
#include "irc/bot/config.pb.h"
#include "irc/connection.h"
#include "irc/message.h"
#include "irc/bot/module.h"
#include "proto/util.h"

namespace irc::bot {

namespace internal {

class BotCore : public ModuleHost {
 public:
  explicit BotCore(event::Loop* loop);

  using ModuleFactory = std::function<std::unique_ptr<Module>(const google::protobuf::Message& config, ModuleHost* host)>;
  void RegisterModule(const std::string& type, ModuleFactory&& factory) {
    module_registry_.try_emplace(type, factory);
  }

  void Start(const google::protobuf::Message& config);
  int Run(const google::protobuf::Message& config);

  Connection* conn(const std::string_view net) override;
  event::Loop* loop() override { return loop_; }
  prometheus::Registry* metric_registry() override { return metric_registry_.get(); }

 private:
  class BotConnection : public Connection, public irc::Connection::Reader {
   public:
    BotConnection(BotCore* core, const ConnectionConfig& cfg, event::Loop* loop, prometheus::Registry* metric_registry, const std::map<std::string, std::string>& metric_labels);
    void Send(const irc::Message& msg) override { core_->SendOn(this, msg); }
    void RawReceived(const irc::Message& msg) override { core_->ReceiveOn(this, msg); }
    const std::string& net() override { return net_; }
   private:
    BotCore* core_;
    const std::string net_;
    std::unique_ptr<irc::Connection> irc_;
    friend class BotCore;
  };

  void SendOn(BotConnection* conn, const irc::Message& msg);
  void ReceiveOn(BotConnection* conn, const irc::Message& msg);

  std::unordered_map<std::string, ModuleFactory> module_registry_;

  event::Loop* loop_;
  std::unique_ptr<event::Loop> private_loop_;

  std::unique_ptr<prometheus::Exposer> metric_exposer_;
  std::shared_ptr<prometheus::Registry> metric_registry_;

  std::vector<std::unique_ptr<BotConnection>> conns_;
  std::vector<std::unique_ptr<Module>> modules_;
};

} // namespace internal

class Bot {
 public:
  explicit Bot(event::Loop* loop = nullptr) : core_(loop) {}

  template <typename ModuleConfigProto, typename ModuleClass>
  void RegisterModule() {
    core_.RegisterModule(
        ModuleConfigProto::descriptor()->full_name(),
        [](const google::protobuf::Message& config, ModuleHost* host) -> std::unique_ptr<Module> {
          return std::make_unique<ModuleClass>(static_cast<const ModuleConfigProto&>(config), host);
        });
  }

  template <typename ModuleConfigProto>
  void RegisterModule(std::function<std::unique_ptr<Module>(const ModuleConfigProto& config, ModuleHost* host)>&& factory) {
    core_.RegisterModule(
      ModuleConfigProto::descriptor()->full_name(),
        [f{std::move(factory)}](const google::protobuf::Message& config, ModuleHost* host) -> std::unique_ptr<Module> {
          return f(static_cast<const ModuleConfigProto&>(config), host);
        });
  }

  void Start(const google::protobuf::Message& config) {
    core_.Start(config);
  }

  int Run(const google::protobuf::Message& config) {
    return core_.Run(config);
  }

  template <typename ConfigProto>
  int Main(int argc, char** argv, event::Loop* loop = nullptr) {
    if (argc != 2) {
      LOG(ERROR) << "usage: " << argv[0] << " <bot.config>";
      return 1;
    }

    ConfigProto config;
    proto::ReadText(argv[1], &config);
    return Run(config);
  }

 private:
  internal::BotCore core_;
};

} // namespace irc::bot

#endif // IRC_BOT_BOT_H_

// Local Variables:
// mode: c++
// End:
