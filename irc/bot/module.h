/** \file
 * IRC bot module interface.
 */

#ifndef IRC_BOT_MODULE_H_
#define IRC_BOT_MODULE_H_

#include <functional>

#include <google/protobuf/descriptor.h>
#include <prometheus/registry.h>

#include "event/loop.h"
#include "irc/message.h"

namespace irc::bot {

struct Connection {
  virtual void Send(const Message& message) = 0;
  virtual const std::string& net() = 0;
  virtual ~Connection() = default;
};

struct ModuleHost {
  virtual Connection* conn(const std::string_view net) = 0;
  virtual event::Loop* loop() = 0;
  virtual prometheus::Registry* metric_registry() = 0;
  virtual ~ModuleHost() = default;
};

struct Module {
  virtual void MessageReceived(Connection* conn, const Message& message);
  virtual void MessageSent(Connection* conn, const Message& message);
  virtual ~Module() = default;
};

} // namespace irc::bot

#endif // IRC_BOT_MODULE_H_

// Local Variables:
// mode: c++
// End:
