#include <cerrno>
#include <vector>

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

int BotCore::Run(const google::protobuf::Message& config) {
  const irc::Config* irc_config = nullptr;
  const irc::bot::Config* bot_config = nullptr;
  std::vector<std::pair<const PluginFactory*, const google::protobuf::Message*>> plugin_configs;

  if (config.GetDescriptor()->full_name() == irc::Config::descriptor()->full_name()) {
    irc_config = static_cast<const irc::Config*>(&config);
  } else if (config.GetDescriptor()->full_name() == irc::bot::Config::descriptor()->full_name()) {
    bot_config = static_cast<const irc::bot::Config*>(&config);
  } else {
    std::vector<const google::protobuf::FieldDescriptor*> fields;
    auto reflection = config.GetReflection();
    reflection->ListFields(config, &fields);

    for (const auto* field : fields) {
      if (field->type() != google::protobuf::FieldDescriptor::TYPE_MESSAGE)
        continue;

      if (field->message_type()->full_name() == irc::Config::descriptor()->full_name()) {
        CHECK(!field->is_repeated());
        irc_config = static_cast<const irc::Config*>(&reflection->GetMessage(config, field));
      } else if (field->message_type()->full_name() == irc::bot::Config::descriptor()->full_name()) {
        CHECK(!field->is_repeated());
        bot_config = static_cast<const irc::bot::Config*>(&reflection->GetMessage(config, field));
      } else {
        auto factory = plugin_registry_.find(field->message_type()->full_name());
        if (factory == plugin_registry_.end())
          continue;
        if (field->is_repeated()) {
          FATAL("TODO: repeated plugin fields");
        } else {
          plugin_configs.emplace_back(&factory->second, &reflection->GetMessage(config, field));
        }
      }
    }
  }

  if (!irc_config && bot_config && bot_config->has_irc())
    irc_config = &bot_config->irc();
  if (!irc_config)
    throw base::Exception("could not find IRC connection configuration");

  if (bot_config) {
    if (!bot_config->metrics_addr().empty()) {
      metric_exposer_ = std::make_unique<prometheus::Exposer>(bot_config->metrics_addr());
      metric_registry_ = std::make_shared<prometheus::Registry>();
      metric_exposer_->RegisterCollectable(metric_registry_);
    }
  }

  for (const auto& plugin_config : plugin_configs) {
    auto plugin = (*plugin_config.first)(*plugin_config.second, this);
    plugins_.push_back(std::move(plugin));
  }

  irc_ = std::make_unique<irc::Connection>(*irc_config, loop_, metric_registry_.get());
  irc_->AddReader(base::borrow(this));
  irc_->Start();
  loop_->Run();
  return 0;
}

void BotCore::Send(const Message& msg) {
  for (const auto& plugin : plugins_)
    plugin->MessageSent(msg);
  irc_->Send(msg);
}

void BotCore::RawReceived(const irc::Message& msg) {
  for (const auto& plugin : plugins_)
    plugin->MessageReceived(msg);

  std::string debug(msg.command());
  for (const std::string& arg : msg.args()) {
    debug += ' ';
    debug += arg;
  }
  LOG(DEBUG) << debug;
}

} // namespace irc::bot::internal
