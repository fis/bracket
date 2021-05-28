#include <cerrno>
#include <memory>
#include <vector>

#include <google/protobuf/reflection.h>
#include <prometheus/registry.h>

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
  std::vector<const irc::Config*> irc_configs;
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
      } else if (field->message_type()->full_name() == irc::Config::descriptor()->full_name()) {
        if (field->is_repeated())
          for (const auto& conn : reflection->GetRepeatedFieldRef<irc::Config>(config, field))
            irc_configs.emplace_back(&conn);
        else
          irc_configs.emplace_back(static_cast<const irc::Config*>(&reflection->GetMessage(config, field)));
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

  if (irc_configs.empty() && bot_config && bot_config->irc_size() > 0)
    for (const auto& conn : bot_config->irc())
      irc_configs.emplace_back(&conn);
  if (irc_configs.empty())
    throw base::Exception("could not find any connection configurations");

  std::map<std::string, std::string> metric_labels;
  if (bot_config) {
    if (!bot_config->metrics_addr().empty()) {
      metric_exposer_ = std::make_unique<prometheus::Exposer>(bot_config->metrics_addr());
      metric_registry_ = std::make_shared<prometheus::Registry>();
      metric_exposer_->RegisterCollectable(metric_registry_);
    }
  }

  for (const irc::Config* irc_config : irc_configs) {
    prometheus::Registry *registry = metric_registry();
    if (registry)
      metric_labels["net"] = irc_config->net();
    conns_.emplace_back(std::make_unique<BotConnection>(this, *irc_config, loop_, registry, metric_labels));
  }

  for (const auto& module_config : module_configs) {
    auto module = (*module_config.first)(*module_config.second, this);
    modules_.push_back(std::move(module));
  }
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

BotConnection::BotConnection(BotCore* core, const irc::Config& cfg, event::Loop* loop, prometheus::Registry* metric_registry, const std::map<std::string, std::string>& metric_labels)
    : core_(core), net_(cfg.net())
{
  irc_ = std::make_unique<irc::Connection>(cfg, loop, metric_registry, metric_labels);
  irc_->AddReader(base::borrow(this));
  irc_->Start();
}

bool BotConnection::on_channel(const std::string_view nick, const std::string_view chan) {
  auto info = nicks_.find(nick);
  return info != nicks_.end() && info->second->on_channel(chan);
}

void BotConnection::RawReceived(const irc::Message& msg) {
  // TODO: implement periodic NAMES queries to handle desync

  if (msg.command_is("JOIN") && msg.nargs() == 1) {
    auto chan = &*chans_.emplace(msg.arg(0)).first;
    TrackJoin(msg.prefix_nick(), chan);
  } else if (msg.command_is("PART") && msg.nargs() >= 1) {
    auto chan = &*chans_.emplace(msg.arg(0)).first;
    TrackPart(msg.prefix_nick(), chan);
  } else if (msg.command_is("353") && msg.nargs() == 4) {
    auto chan = &*chans_.emplace(msg.arg(2)).first;
    std::string_view tail = msg.arg(3);
    while (!tail.empty()) {
      while (tail.find_last_of(" @+", 0) == 0)
        tail.remove_prefix(1);
      std::string_view nick_name = tail;
      if (auto sep = nick_name.find(' '); sep != nick_name.npos)
        nick_name = nick_name.substr(0, sep);
      if (nick_name.empty())
        break;
      TrackJoin(nick_name, chan);
      tail.remove_prefix(nick_name.size());
    }
  }

  core_->ReceiveOn(this, msg);

  if (msg.command_is("NICK") && msg.nargs() == 1) {
    if (auto old = nicks_.find(msg.prefix_nick()); old != nicks_.end()) {
      std::unique_ptr<Nick> nick = std::move(old->second);
      nicks_.erase(old);
      nick->name = msg.arg(0);
      nicks_.emplace(nick->name, std::move(nick));
    } else {
      auto nick = std::make_unique<Nick>(msg.prefix_nick());
      nicks_.emplace(nick->name, std::move(nick));
    }
  } else if (msg.command_is("QUIT")) {
    nicks_.erase(msg.prefix_nick());
  }
}

void BotConnection::TrackJoin(const std::string_view nick_name, const std::string* chan) {
  if (auto old = nicks_.find(nick_name); old != nicks_.end()) {
    if (!old->second->on_channel(*chan))
      old->second->chans.push_back(chan);
  } else {
    auto nick = std::make_unique<Nick>(nick_name);
    nick->chans.push_back(chan);
    nicks_.emplace(nick->name, std::move(nick));
  }
}

void BotConnection::TrackPart(const std::string_view nick_name, const std::string* chan) {
  if (auto old = nicks_.find(nick_name); old != nicks_.end()) {
    auto& nick_chans = old->second->chans;
    if (auto ex = std::find(nick_chans.begin(), nick_chans.end(), chan); ex != nick_chans.end())
      nick_chans.erase(ex);
    if (nick_chans.empty())
      nicks_.erase(old);
  }
}

} // namespace irc::bot::internal
