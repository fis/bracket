#include <cerrno>

#include "base/exc.h"
#include "base/log.h"
#include "irc/bot/bot.h"
#include "irc/bot/config.pb.h"

extern "C" {
#include <dlfcn.h>
}

namespace irc::bot {

Bot::Bot(const std::function<std::unique_ptr<google::protobuf::Message>()>& config_loader, event::Loop* loop)
    : config_loader_(config_loader)
{
  if (loop) {
    loop_ = loop;
  } else {
    private_loop_ = std::make_unique<event::Loop>();
    loop_ = private_loop_.get();
  }

  Load();
}

void Bot::Run() {
  irc_->Start();
  while (true)
    loop_->Poll();
}

void Bot::Send(const Message& msg) {
  for (const auto& plugin : plugins_)
    plugin->MessageSent(msg);
  irc_->Send(msg);
}

void Bot::MessageReceived(const irc::Message& msg) {
  for (const auto& plugin : plugins_)
    plugin->MessageReceived(msg);

  std::string debug(msg.command());
  for (const std::string& arg : msg.args()) {
    debug += ' ';
    debug += arg;
  }
  LOG(DEBUG) << debug;
}

void Bot::Load() {
  auto config = config_loader_();

  // locate the basic IRC connection and bot config elements

  const irc::Config* irc_config = nullptr;
  const BotConfig* bot_config = nullptr;

  if (config->GetDescriptor()->full_name() == irc::Config::descriptor()->full_name()) {
    irc_config = static_cast<const irc::Config*>(config.get());
  } else if (config->GetDescriptor()->full_name() == BotConfig::descriptor()->full_name()) {
    bot_config = static_cast<const BotConfig*>(config.get());
  } else {
    std::vector<const google::protobuf::FieldDescriptor*> fields;
    auto reflection = config->GetReflection();
    reflection->ListFields(*config, &fields);

    for (const auto* field : fields) {
      if (field->type() != google::protobuf::FieldDescriptor::TYPE_MESSAGE || field->is_repeated())
        continue;
      if (field->message_type()->full_name() == irc::Config::descriptor()->full_name())
        irc_config = static_cast<const irc::Config*>(&reflection->GetMessage(*config, field));
      else if (field->message_type()->full_name() == BotConfig::descriptor()->full_name())
        bot_config = static_cast<const BotConfig*>(&reflection->GetMessage(*config, field));
    }
  }

  if (!irc_config && bot_config && bot_config->has_irc())
    irc_config = &bot_config->irc();

  // create the IRC connection, if this is the first load

  if (!irc_) {
    if (!irc_config)
      throw base::Exception("could not find IRC connection config field");
    irc_ = std::make_unique<irc::Connection>(*irc_config, loop_);
    irc_->AddReader(this);
  }

  // load any dynamic modules

  if (bot_config && bot_config->modules_size()) {
    for (const auto& module_file : bot_config->modules()) {
      LOG(INFO) << "Loading module: " << module_file;

      void* module_handle = dlopen(module_file.c_str(), RTLD_LAZY | RTLD_LOCAL);
      if (!module_handle)
        throw base::Exception("dlopen", errno);

      auto module_init = (void (*)(const internal::PluginRegistrar&)) dlsym(module_handle, "irc_bot_module_init");
      if (!module_init) {
        dlclose(module_handle);
        throw base::Exception("no initialization function in module");
      }

      module_handles_.push_back(module_handle);
      module_init(
          [this](auto desc, auto create, auto destroy) {
            plugin_registry_.try_emplace(desc->full_name(), create, destroy);
          });
    }
  }

  // create all configured plugins

  {
    std::vector<const google::protobuf::FieldDescriptor*> fields;
    auto reflection = config->GetReflection();
    reflection->ListFields(*config, &fields);

    for (const auto* field : fields) {
      if (field->type() != google::protobuf::FieldDescriptor::TYPE_MESSAGE)
        continue;

      auto plugin_factory = plugin_registry_.find(field->message_type()->full_name());
      if (plugin_factory == plugin_registry_.end())
        continue;

      LOG(INFO) << "Creating plugin: " << field->message_type()->full_name();

      plugins_.emplace_back(
          plugin_factory->second.first(reflection->GetMessage(*config, field), this),
          plugin_factory->second.second);
    }
  }
}

} // namespace irc::bot
