#include "irc/bot/module.h"

namespace irc::bot {

void Module::MessageReceived(Connection* conn, const Message& message) {}
void Module::MessageSent(Connection* conn, const Message& message) {}

} // namespace irc::bot
