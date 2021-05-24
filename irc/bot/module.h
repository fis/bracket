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

struct ModuleHost {
  virtual void Send(const Message& message) = 0;
  virtual event::Loop* loop() = 0;
  virtual prometheus::Registry* metric_registry() = 0;
  virtual ~ModuleHost() = default;
};

class Module {
 public:
  virtual void MessageReceived(const Message& message);
  virtual void MessageSent(const Message& message);
  virtual ~Module() = default;
};

} // namespace irc::bot

#endif // IRC_BOT_MODULE_H_

// Local Variables:
// mode: c++
// End:
