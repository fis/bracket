#include <cerrno>

#include "base/exc.h"
#include "base/log.h"
#include "irc/bot/bot.h"

namespace irc::bot {

void Bot::Run() {
  // process the configuration proto and create plugins

  auto config = LoadConfig();

  const irc::Config* irc_config = nullptr;

  if (config->GetDescriptor()->full_name() == irc::Config::descriptor()->full_name()) {
    irc_config = static_cast<const irc::Config*>(config.get());
  } else {
    std::vector<const google::protobuf::FieldDescriptor*> fields;
    auto reflection = config->GetReflection();
    reflection->ListFields(*config, &fields);

    for (const auto* field : fields) {
      if (field->type() != google::protobuf::FieldDescriptor::TYPE_MESSAGE)
        continue;

      if (field->message_type()->full_name() == irc::Config::descriptor()->full_name()) {
        CHECK(!field->is_repeated());
        irc_config = static_cast<const irc::Config*>(&reflection->GetMessage(*config, field));
      } else {
        auto factory = plugin_registry_.find(field->message_type()->full_name());
        if (factory == plugin_registry_.end())
          continue;
        if (field->is_repeated()) {
          FATAL("TODO: repeated plugin fields");
        } else {
          LOG(INFO) << "Creating plugin for: " << field->message_type()->full_name();
          auto plugin = factory->second(reflection->GetMessage(*config, field), this);
          plugins_.push_back(std::move(plugin));
        }
      }
    }
  }

  // start the IRC connection and run

  if (!irc_config)
    throw base::Exception("could not find IRC connection config field");

  irc_ = std::make_unique<irc::Connection>(*irc_config, loop_);
  irc_->AddReader(this);
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

Bot::Bot(event::Loop* loop) {
  if (loop) {
    loop_ = loop;
  } else {
    private_loop_ = std::make_unique<event::Loop>();
    loop_ = private_loop_.get();
  }
}

} // namespace irc::bot
