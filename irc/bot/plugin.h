/** \file
 * IRC bot plugin interface.
 */

#ifndef IRC_BOT_PLUGIN_H_
#define IRC_BOT_PLUGIN_H_

#include <functional>

#include <google/protobuf/descriptor.h>

#include "event/loop.h"
#include "irc/message.h"

namespace irc::bot {

struct PluginHost {
  virtual void Send(const Message& message) = 0;
  virtual event::Loop* loop() = 0;
  virtual ~PluginHost() = default;
};

class Plugin {
 public:
  virtual void MessageReceived(const Message& message);
  virtual void MessageSent(const Message& message);
  virtual ~Plugin() = default;
};

} // namespace irc::bot

/**
 * Declares a new dynamically loaded plugin module for the irc::bot library.
 *
 * The argument must be a list of pairs of type names `C1`, `P1`, `C2`, `P2`, ..., containing at
 * least one pair. The types must satisfy the following requirements.
 *
 * - `C` must normally be the name of a generated protobuf message class, or at least have a static
 *   method `descriptor()` returning a `const google::protobuf::Descriptor*`.
 *
 * - The `P` type must be a class that inherits from from irc::bot::Plugin.
 *
 * - The `P` type must have a one-argument constructor taking a `const C&`.
 */
#define IRC_BOT_MODULE(...) \
  extern "C" void irc_bot_module_init(const ::irc::bot::internal::PluginRegistrar& registrar) { \
    ::irc::bot::internal::RegisterPlugins<__VA_ARGS__>::Register(registrar); \
  } \
  extern "C" void irc_bot_module_init(const ::irc::bot::internal::PluginRegistrar& registrar)

// implementation details for the IRC_BOT_MODULE macro
namespace irc::bot::internal {

template <typename C, typename P>
struct PluginFactory {
  static Plugin* Create(const google::protobuf::Message& config, irc::bot::PluginHost* host) {
    return new P(static_cast<const C&>(config), host);
  }
  static void Destroy(Plugin* plugin) {
    delete plugin;
  }
};

using PluginCreator = Plugin*(const google::protobuf::Message&, irc::bot::PluginHost*);
using PluginDestroyer = void(Plugin*);
using PluginRegistrar = std::function<void(const google::protobuf::Descriptor*, PluginCreator*, PluginDestroyer*)>;

template <typename... Ts>
struct RegisterPlugins;

template <>
struct RegisterPlugins<> {
  static void Register(const PluginRegistrar& registrar) {}
};

template <typename C1, typename P1, typename... Ts>
struct RegisterPlugins<C1, P1, Ts...> {
  static void Register(const PluginRegistrar& registrar) {
    using Factory = PluginFactory<C1, P1>;
    registrar(C1::descriptor(), &Factory::Create, &Factory::Destroy);
    RegisterPlugins<Ts...>::Register(registrar);
  }
};

} // namespace irc::bot::internal

#endif // IRC_BOT_PLUGIN_H_

// Local Variables:
// mode: c++
// End:
