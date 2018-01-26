/** \file
 * IRC bot plugin interface.
 */

#ifndef IRC_BOT_PLUGIN_H_
#define IRC_BOT_PLUGIN_H_

#include <functional>

#include <google/protobuf/descriptor.h>
#include <prometheus/registry.h>

#include "event/loop.h"
#include "irc/message.h"

namespace irc::bot {

struct PluginHost {
  virtual void Send(const Message& message) = 0;
  virtual event::Loop* loop() = 0;
  virtual prometheus::Registry* metric_registry() = 0;
  virtual ~PluginHost() = default;
};

class Plugin {
 public:
  virtual void MessageReceived(const Message& message);
  virtual void MessageSent(const Message& message);
  virtual ~Plugin() = default;
};

} // namespace irc::bot

#endif // IRC_BOT_PLUGIN_H_

// Local Variables:
// mode: c++
// End:
