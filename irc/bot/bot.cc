#include <cerrno>
#include <memory>
#include <vector>

#include <google/protobuf/reflection.h>

#include "base/exc.h"
#include "base/log.h"
#include "irc/config.pb.h"
#include "irc/bot/bot.h"
#include "irc/bot/config.pb.h"

namespace irc::bot::internal {

BotCore::BotCore(event::Loop* loop) {
  if (loop) {
    loop_ = loop;
  } else {
    private_loop_ = std::make_unique<event::Loop>();
    loop_ = private_loop_.get();
  }
}

void BotCore::Start(const google::protobuf::Message& config) {
  const irc::bot::Config* bot_config = nullptr;
  std::vector<const irc::bot::ConnectionConfig*> conn_configs;
  std::vector<std::pair<const ModuleFactory*, const google::protobuf::Message*>> module_configs;

  if (config.GetDescriptor()->full_name() == irc::bot::Config::descriptor()->full_name()) {
    bot_config = static_cast<const irc::bot::Config*>(&config);
  } else {
    std::vector<const google::protobuf::FieldDescriptor*> fields;
    auto reflection = config.GetReflection();
    reflection->ListFields(config, &fields);

    for (const auto* field : fields) {
      if (field->type() != google::protobuf::FieldDescriptor::TYPE_MESSAGE)
        continue;

      if (field->message_type()->full_name() == irc::bot::Config::descriptor()->full_name()) {
        CHECK(!field->is_repeated());
        bot_config = static_cast<const irc::bot::Config*>(&reflection->GetMessage(config, field));
      } else if (field->message_type()->full_name() == irc::bot::ConnectionConfig::descriptor()->full_name()) {
        if (field->is_repeated())
          for (const auto& conn : reflection->GetRepeatedFieldRef<irc::bot::ConnectionConfig>(config, field))
            conn_configs.emplace_back(&conn);
        else
          conn_configs.emplace_back(static_cast<const irc::bot::ConnectionConfig*>(&reflection->GetMessage(config, field)));
      } else {
        auto factory = module_registry_.find(field->message_type()->full_name());
        if (factory == module_registry_.end())
          continue;
        if (field->is_repeated())
          for (const auto& msg : reflection->GetRepeatedFieldRef<google::protobuf::Message>(config, field))
            module_configs.emplace_back(&factory->second, &msg);
        else
          module_configs.emplace_back(&factory->second, &reflection->GetMessage(config, field));
      }
    }
  }

  if (conn_configs.empty() && bot_config && bot_config->conns_size() > 0)
    for (const auto& conn : bot_config->conns())
      conn_configs.emplace_back(&conn);
  if (conn_configs.empty())
    throw base::Exception("could not find any connection configurations");

  if (bot_config) {
    if (!bot_config->metrics_addr().empty()) {
      metric_exposer_ = std::make_unique<prometheus::Exposer>(bot_config->metrics_addr());
      metric_registry_ = std::make_shared<prometheus::Registry>();
      metric_exposer_->RegisterCollectable(metric_registry_);
    }
  }

  for (const ConnectionConfig* conn_config : conn_configs)
    conns_.emplace_back(std::make_unique<BotConnection>(this, *conn_config, loop_, metric_registry()));

  for (const auto& module_config : module_configs) {
    auto module = (*module_config.first)(*module_config.second, this);
    modules_.push_back(std::move(module));
  }
}

BotCore::BotConnection::BotConnection(BotCore* core, const ConnectionConfig& cfg, event::Loop* loop, prometheus::Registry* metric_registry)
    : core_(core), net_(cfg.net())
{
  irc_ = std::make_unique<irc::Connection>(cfg.irc(), loop, metric_registry);
  irc_->AddReader(base::borrow(this));
  irc_->Start();
}

int BotCore::Run(const google::protobuf::Message& config) {
  Start(config);
  loop_->Run();
  return 0;
}

Connection* BotCore::conn(const std::string_view net) {
  for (const auto& conn : conns_)
    if (conn->net_ == net)
      return conn.get();
  return nullptr;
}

void BotCore::SendOn(BotConnection* conn, const irc::Message& msg) {
  for (const auto& module : modules_)
    module->MessageSent(conn, msg);

  conn->irc_->Send(msg);
}

void BotCore::ReceiveOn(BotConnection* conn, const irc::Message& msg) {
  for (const auto& module : modules_)
    module->MessageReceived(conn, msg);

  if (LOG_ENABLED(DEBUG)) {
    std::string debug(msg.command());
    for (const std::string& arg : msg.args()) {
      debug += ' ';
      debug += arg;
    }
    LOG(DEBUG) << debug;
  }
}

} // namespace irc::bot::internal
