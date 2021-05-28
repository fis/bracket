#include <algorithm>

#include <google/protobuf/repeated_field.h>

#include "irc/bot/remote.h"

namespace irc::bot {

namespace {

void EventToMessage(const IrcEvent& event, Message* message) {
  message->set_prefix(event.prefix());
  message->set_command(event.command());
  auto* args = message->mutable_args();
  args->insert(args->end(), event.args().begin(), event.args().end());
}

void MessageToEvent(const Message& message, IrcEvent* event, bool sent) {
  event->set_prefix(message.prefix());
  event->set_command(message.command());
  std::copy(message.args().begin(), message.args().end(),
            google::protobuf::RepeatedFieldBackInserter(event->mutable_args()));
  if (sent)
    event->set_direction(IrcEvent::SENT);
}

} // unnamed namespace

Remote::Remote(const RemoteConfig& config, irc::bot::ModuleHost* host)
    : host_(host), server_(host->loop(), base::borrow(this))
{
  auto err = server_.Start(config.socket_path());
  if (err)
    throw new base::Exception(*err);
}

base::optional_ptr<Remote::WatchHandler> Remote::Watch(WatchCall* call) {
  return base::borrow(watchers_.emplace(call, this));
}

void Remote::ActiveWatcher::WatchMessage(WatchCall*, const ::irc::bot::WatchRequest& req) {
  nets_.clear();
  nets_.insert(nets_.end(), req.nets().begin(), req.nets().end());
}

void Remote::ActiveWatcher::WatchClose(WatchCall* call, ::base::error_ptr error) {
  if (error)
    LOG(WARNING) << "remote: " << *error;
  remote_->watchers_.erase(this); // self-destruct
}

bool Remote::SendTo(const ::irc::bot::SendToRequest& req, ::google::protobuf::Empty* resp) {
  LOG(WARNING) << "SendTo request: " << req.ShortDebugString();
  Connection* conn = host_->conn(req.net());
  if (!conn)
    return false;
  Message msg;
  EventToMessage(req.event(), &msg);
  conn->Send(msg);
  return true;
}

void Remote::RemoteServiceError(::base::error_ptr error) {
  LOG(WARNING) << "remote: " << *error;
}

void Remote::MessageReceived(Connection* conn, const Message& message) {
  for (const auto& watcher : watchers_)
    watcher->MessageReceived(conn, message);
}

void Remote::ActiveWatcher::MessageReceived(Connection* conn, const Message& message) {
  if (std::find(nets_.begin(), nets_.end(), conn->net()) == nets_.end())
    return;
  IrcEvent event;
  MessageToEvent(message, &event, /* sent= */ false);
  call_->Send(event);
}

void Remote::MessageSent(Connection* conn, const Message& message) {
  for (const auto& watcher : watchers_)
    watcher->MessageSent(conn, message);
}

void Remote::ActiveWatcher::MessageSent(Connection* conn, const Message& message) {
  if (std::find(nets_.begin(), nets_.end(), conn->net()) == nets_.end())
    return;
  IrcEvent event;
  MessageToEvent(message, &event, /* sent= */ true);
  call_->Send(event);
}

} // namespace irc::bot
