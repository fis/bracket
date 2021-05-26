#include <cerrno>
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>
#include <system_error>
#include <unordered_map>

#include <openssl/err.h>
#include <prometheus/counter.h>
#include <prometheus/gauge.h>

#include "base/log.h"
#include "irc/config.pb.h"
#include "irc/connection.h"

extern "C" {
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
}

namespace irc {

// Custom logging formatting for special types.
// ============================================

std::ostream& operator<<(std::ostream& os, const Config::Server& server) {
  os << server.host() << ':' << server.port();
  return os;
}

// Connection implementation.
// ==========================

constexpr auto kAutoJoinDelay = std::chrono::seconds(30);
constexpr auto kNickRegainDelay = std::chrono::seconds(120);

// TODO sort methods?

Connection::Connection(const Config& config, event::Loop* loop, prometheus::Registry* metric_registry, const std::map<std::string, std::string>& metric_labels)
    : loop_(loop)
{
  // configuration defaults
  config_.set_user("bracket");
  config_.set_realname("bracket");
  config_.set_resolve_timeout_ms(30000);
  config_.set_connect_timeout_ms(60000);
  config_.set_reconnect_delay_ms(30000);
  config_.MergeFrom(config);

  if (config_.has_sasl() && config_.sasl().mech() == SaslMechanism::PLAIN
      && config_.sasl().pass().empty() && config_.pass().empty())
    throw base::Exception("SASL PLAIN configured globally but password not provided");
  for (const auto& server : config_.servers())
    if (server.has_sasl() && server.sasl().mech() == SaslMechanism::PLAIN
        && server.sasl().pass().empty() && server.pass().empty() && config.pass().empty())
      throw base::Exception("SASL PLAIN configured for a server but password not provided");
  if (config_.nick().empty())
    throw base::Exception("IRC nickname not configured");
  for (const auto& channel : config_.channels())
    channels_[channel] = ChannelState::kKnown;

  if (metric_registry) {
    metric_connection_up_ = &prometheus::BuildGauge()
        .Name("irc_connection_up")
        .Help("Is the bot currently connected to an IRC server?")
        .Register(*metric_registry)
        .Add(metric_labels);
    metric_sent_bytes_ = &prometheus::BuildCounter()
        .Name("irc_sent_bytes")
        .Help("How many bytes have been sent to the IRC server?")
        .Register(*metric_registry)
        .Add(metric_labels);
    metric_sent_lines_ = &prometheus::BuildCounter()
        .Name("irc_sent_lines")
        .Help("How many lines (commands) have been sent to the IRC server?")
        .Register(*metric_registry)
        .Add(metric_labels);
    metric_received_bytes_ = &prometheus::BuildCounter()
        .Name("irc_received_bytes")
        .Help("How many bytes have been received from the IRC server?")
        .Register(*metric_registry)
        .Add(metric_labels);
    metric_received_lines_ = &prometheus::BuildCounter()
        .Name("irc_received_lines")
        .Help("How many lines (commands) have been received from the IRC server?")
        .Register(*metric_registry)
        .Add(metric_labels);
    metric_write_queue_bytes_ = &prometheus::BuildGauge()
        .Name("irc_write_queue_bytes")
        .Help("How many bytes are pending in the write queue?")
        .Register(*metric_registry)
        .Add(metric_labels);
  }
}

Connection::~Connection() {
  if (socket_)
    LOG(WARNING) << "active connection to " << config_.servers(current_server_) << " destroyed.";

  if (reconnect_timer_)
    loop_->CancelTimer(reconnect_timer_);
  if (write_credit_timer_)
    loop_->CancelTimer(write_credit_timer_);
}

void Connection::Start() {
  if (socket_)  // already running
    return;

  if (current_server_ >= config_.servers_size())
    throw base::Exception("server configuration not found");
  const Config::Server& server = config_.servers(current_server_);
  const TlsConfig* tls = server.has_tls() ? &server.tls() : config_.has_tls() ? &config_.tls() : nullptr;
  sasl_ = server.has_sasl() ? &server.sasl() : config_.has_sasl() ? &config_.sasl() : nullptr;

  event::Socket::Builder builder;

  builder
      .loop(loop_)
      .watcher(this)
      .host(server.host())
      .port(server.port())
      .resolve_timeout_ms(config_.resolve_timeout_ms())
      .connect_timeout_ms(config_.connect_timeout_ms());

  if (tls)
    builder
        .tls(true)
        .client_cert(tls->client_cert())
        .client_key(tls->client_key());

  auto maybe_socket = builder.Build();
  if (!maybe_socket.ok()) {
    ConnectionLost(maybe_socket.error());
    return;
  }
  socket_ = maybe_socket.ptr();
  socket_->Start();
  state_ = kConnecting;
}

void Connection::Stop() {
}

void Connection::ConnectionOpen() {
  LOG(INFO) << "connected to " << config_.servers(current_server_);

  pass_ = nullptr;
  if (!config_.servers(current_server_).pass().empty())
    pass_ = &config_.servers(current_server_).pass();
  else if (!config_.pass().empty())
    pass_ = &config_.pass();

  if (sasl_)
    SendNow({ "CAP", "LS", "302" });
  if (pass_)
    SendNow({ "PASS", *pass_ });
  SendNow({ "NICK", config_.nick() });
  SendNow({ "USER", config_.user(), "0", "*", config_.realname() });

  nick_ = config_.nick();
  alt_nick_ = 0;

  auto_join_timer_ = loop_->Delay(kAutoJoinDelay, base::borrow(&auto_join_timer_callback_));

  socket_->WantRead(true);

  if (metric_connection_up_)
    metric_connection_up_->Set(1);
}

void Connection::ConnectionFailed(base::error_ptr error) {
  ConnectionLost(std::move(error));
}

void Connection::CanRead() {
  std::size_t got;

  {
    base::io_result ret = socket_->Read(read_buffer_.data() + read_buffer_used_, read_buffer_.size() - read_buffer_used_);
    if (!ret.ok()) {
      ConnectionLost(ret.error());
      return;
    }
    got = ret.size();
  }

  if (got == 0)
    return;

  read_buffer_used_ += got;
  if (metric_received_bytes_)
    metric_received_bytes_->Increment(got);

  // parse complete messages and pass them to listeners

  auto* start = read_buffer_.data();
  std::size_t left = read_buffer_used_;

  while (left > 0) {
    // read next complete message at 'start'

    std::size_t msg_len = 0;
    while (msg_len < left && msg_len < kMaxMessageSize && start[msg_len] != 10 && start[msg_len] != 13)
      ++msg_len;

    if (msg_len == kMaxMessageSize || start[msg_len] == 10 || start[msg_len] == 13) {
      // found a delimiter, or reached maximum message size
      if (msg_len > 0) {
        if (read_message_.Parse(start, msg_len))
          HandleMessage(read_message_);
        else
          LOG(ERROR) << "invalid IRC message";  /// \todo dump bytes?
        if (metric_received_lines_)
          metric_received_lines_->Increment();
      }
    } else {
      // hit end of buffer with an incomplete message
      break;
    }

    // consume delimiters that followed the message

    start += msg_len;
    left -= msg_len;
    while (left > 0 && (*start == 10 || *start == 13)) {
      ++start;
      --left;
    }
  }

  // save any incomplete data for the next round

  read_buffer_used_ = left;
  if (left > 0)
    std::memmove(read_buffer_.data(), start, left);

  read_message_.Clear();
}

void Connection::HandleMessage(const Message& message) {
  // standard actions

  if (message.command_is("CAP")) {
    // CAP -- capability negotiation in progress
    if (message.arg_is(1, "LS")) {
      // CAP * LS -- capability negotiation response
      if (message.arg_is(2, "*") && message.nargs() == 4) {
        // CAP * LS * :... -- multiline capability reply with continuation lines expected, add to set
        AddCaps(message.arg(3));
      } else {
        // CAP * LS ... -- final capability reply, add to set and then request caps or end negotiation
        if (message.nargs() == 3)
          AddCaps(message.arg(2));
        ReqNeededCaps();
      }
    } else if (message.arg_is(1, "ACK") && message.nargs() == 3) {
      // CAP * ACK :... -- successfully enabled requested capabilities, start auth or end negotiation
      // TODO: this should really keep a set of requested capabilities and validate the ACK
      EndCaps(true);
    } else if (message.arg_is(1, "NAK") && message.nargs() == 3) {
      // CAP * NAK :... -- failed to enable requested capabilities, end negotiation anyway
      EndCaps(false);
    }
  } else if (sasl_ && message.command_is("AUTHENTICATE") && message.arg_is(0, "+")) {
    RespondSasl();
  } else if (message.command_is("902") || message.command_is("903") || message.command_is("904") || message.command_is("905") || message.command_is("906") || message.command_is("907")) {
    // ERR_NICKLOCKED (902), RPL_SASLSUCCESS (903), ERR_SASLFAIL (904), ERR_SASLTOOLONG (905), ERR_SASLABORTED (906), ERR_SASLALREADY (907):
    // success or terminal error codes of SASL authentication, finish the registration sequence
    SendNow({ "CAP", "END" });
  } else if (message.command_is("001")) {
    // RPL_WELCOME -- successful registration
    Registered();
  } if (message.command_is("376")) {
    // RPL_ENDOFMOTD -- trigger autojoin (if not done yet)
    if (auto_join_timer_ != event::kNoTimer) {
      loop_->CancelTimer(auto_join_timer_);
      AutoJoinTimer();
    }
  } else if (message.command_is("433") || message.command_is("437")) {
    // ERR_NICKNAMEINUSE | ERR_UNAVAILRESOURCE -- try alt nick if registering, or restart nick regain timer
    if (state_ == kConnecting) {
      nick_ = config_.nick() + std::to_string(++alt_nick_);
      SendNow({ "NICK", nick_.c_str() });
    } else if (nick_regain_timer_ == event::kNoTimer) {
      nick_regain_timer_ = loop_->Delay(kNickRegainDelay, base::borrow(&nick_regain_timer_callback_));
    }
  } else if (message.command_is("JOIN")) {
    if (message.prefix_nick_is(nick_) && message.nargs() >= 1) {
      auto record = channels_.find(message.arg(0));
      if (record != channels_.end()) {
        record->second = ChannelState::kJoined;
        readers_.Call(&Reader::ChannelJoined, record->first);
      }
    }
  } else if (message.command_is("NICK")) {
    if (message.prefix_nick_is(nick_) && message.nargs() >= 1) {
      nick_ = message.arg(0);
      readers_.Call(&Reader::NickChanged, nick_);
    }
  } else if (message.command_is("PING")) {
    SendNow({ "PONG", message.nargs() == 1 ? message.arg(0).c_str() : config_.nick().c_str() });
  }

  // pass to client

  readers_.Call(&Reader::RawReceived, read_message_);
}

void Connection::AddCaps(const std::string& spec) {
  // TODO FIXME parse caps
}

void Connection::ReqNeededCaps() {
  if (sasl_)
    SendNow({ "CAP", "REQ", "sasl" });
  else
    SendNow({ "CAP", "END" });
}

void Connection::EndCaps(bool ack) {
  if (ack && sasl_)
    StartSasl();
  else
    SendNow({ "CAP", "END" });
}

struct SaslMechInfo {
  const char* names[SaslMechanism_ARRAYSIZE];
  constexpr SaslMechInfo() : names{} {
    names[SaslMechanism::PLAIN] = "PLAIN";
    names[SaslMechanism::EXTERNAL] = "EXTERNAL";
  }
};
constexpr static SaslMechInfo kSaslMechInfo;

void Connection::StartSasl() {
  SendNow({ "AUTHENTICATE", kSaslMechInfo.names[sasl_->mech()]});
}

// TODO: move into a utility
static void EncodeB64(std::string* out, const std::string& in) {
  static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

  auto tail = base::to_const_byte_view(in);
  while (tail.size() >= 3) {
    auto chunk = (std::uint32_t) tail[0] << 16 | (std::uint32_t) tail[1] << 8 | tail[2];
    out->push_back(alphabet[chunk >> 18]);
    out->push_back(alphabet[(chunk >> 12) & 0x3f]);
    out->push_back(alphabet[(chunk >> 6) & 0x3f]);
    out->push_back(alphabet[chunk & 0x3f]);
    tail.pop_front(3);
  }
  if (tail.size() == 2) {
    std::uint32_t chunk = (std::uint32_t) tail[0] << 10 | (std::uint32_t) tail[1] << 2;
    out->push_back(alphabet[chunk >> 12]);
    out->push_back(alphabet[(chunk >> 6) & 0x3f]);
    out->push_back(alphabet[chunk & 0x3f]);
    out->push_back('=');
  } else if (tail.size() == 1) {
    std::uint32_t chunk = (std::uint32_t) tail[0] << 4;
    out->push_back(alphabet[chunk >> 6]);
    out->push_back(alphabet[chunk & 0x3f]);
    out->push_back('=');
    out->push_back('=');
  }
}

void Connection::RespondSasl() {
  const std::string* authz = &sasl_->authz();
  if (authz->empty())
    authz = &config_.nick();

  std::string resp;
  switch (sasl_->mech()) {
  case SaslMechanism::PLAIN:
    {
      const std::string* authc = &sasl_->authc();
      if (authc->empty())
        authc = authz;
      const std::string* pass = &sasl_->pass();
      if (pass->empty())
        pass = pass_;
      std::string combined;
      combined.append(*authz);
      combined.push_back(0);
      combined.append(*authc);
      combined.push_back(0);
      combined.append(*pass);
      EncodeB64(&resp, combined);
    }
    break;

  case SaslMechanism::EXTERNAL:
    EncodeB64(&resp, *authz);
    break;

  default:
    resp = "*";
    break;
  }

  SendNow({ "AUTHENTICATE", resp });
}

namespace {

/** IRC commands that get an extra surcharge. */
const std::unordered_map<std::string, int> extra_cost = {
  { "JOIN", 1000 },
  { "NICK", 1000 },
  { "PART", 1000 },
  { "PING", 1000 },
  { "USERHOST", 1000 },
  { "KICK", 2000 },
  { "MODE", 2000 },
  { "TOPIC", 2000 },
  { "WHO", 3000 },
};

} // unnamed namespace

void Connection::Send(const Message& message) {
  if (state_ != kReady)
    return;
  SendNow(message);
}

void Connection::SendNow(const Message& message) {
  bool was_empty = write_queue_.empty();

  constexpr std::size_t kMaxContentSize = kMaxMessageSize - 2;

  auto buffer = write_buffer_.push(kMaxContentSize);
  std::size_t message_size = message.Write(buffer.first.data(), buffer.first.size());  // first half
  std::size_t write_size = std::min(message_size, kMaxContentSize);

  if (write_size > buffer.first.size()) {
    // didn't fit, so do trailing half
    CHECK(buffer.second.valid() && write_size <= buffer.first.size() + buffer.second.size());
    unsigned char tmp_buffer[write_size];
    message.Write(tmp_buffer, write_size);
    std::memcpy(buffer.second.data(), tmp_buffer + buffer.first.size(), write_size - buffer.first.size());
  }

  if (write_size < kMaxContentSize)
    write_buffer_.unpush(kMaxContentSize - write_size);

  write_buffer_.write_u8(13);
  write_buffer_.write_u8(10);

  int cost = 1000;
  auto extra_cost_it = extra_cost.find(message.command());
  if (extra_cost_it != extra_cost.end())
    cost += extra_cost_it->second;

  write_queue_.emplace_back(write_size + 2, cost);
  LOG(VERBOSE) << "added " << write_size + 2 << " bytes to the write queue (cost " << cost << ')';

  if (metric_write_queue_bytes_)
    metric_write_queue_bytes_->Set(write_buffer_.size());

  if (was_empty)  // otherwise we're trying already
    Flush();
}

void Connection::CanWrite() {
  Flush();
}

void Connection::Flush() {
  if (write_queue_.empty()) {  // nothing to write
    socket_->WantWrite(false);
    return;
  }

  // update credit estimate

  if (write_credit_ < kMaxWriteCredit) {
    auto now = loop_->now();
    int delta =
        std::max(std::min(std::chrono::duration_cast<std::chrono::milliseconds>(now - write_credit_time_),
                          std::chrono::milliseconds(kMaxWriteCredit)),
                 std::chrono::milliseconds(0)).count();
    write_credit_ = std::min(write_credit_ + delta, kMaxWriteCredit);
    write_credit_time_ = now;
  }

  // see how much we can write

  std::size_t can_write = 0;
  int credit_left = write_credit_;

  for (auto&& msg : write_queue_) {
    int cost = 10 * msg.first + msg.second;
    if (cost > credit_left)
      break;
    can_write += msg.first;
    credit_left -= cost;
  }

  // try to send that much

  std::size_t wrote = 0;

  if (can_write > 0) {
    LOG(VERBOSE) << "try to write " << can_write << " bytes to server";

    auto data = write_buffer_.front(can_write);
    for (const base::byte_view& slice : { data.first, data.second }) {
      if (slice.size() == 0)
        break;

      base::io_result ret = socket_->Write(slice.data(), slice.size());
      if (!ret.ok()) {
        ConnectionLost(ret.error());
        return;
      }

      wrote += ret.size();
      if (ret.size() != slice.size())
        break;  // couldn't fit all of it in
    }
  }

  // pop off what we managed to write

  if (wrote > 0) {
    if (metric_sent_bytes_)
      metric_sent_bytes_->Increment(wrote);

    write_buffer_.pop(wrote);
    if (metric_write_queue_bytes_)
      metric_write_queue_bytes_->Set(write_buffer_.size());

    std::size_t pop = wrote;
    while (pop > 0) {
      CHECK(!write_queue_.empty());
      auto& msg = write_queue_.front();
      std::size_t first_bytes = msg.first;
      if (first_bytes <= pop) {
        pop -= first_bytes;
        write_credit_ -= 10 * msg.first + msg.second;
        write_queue_.pop_front();
        if (metric_sent_lines_)
          metric_sent_lines_->Increment();
      } else {
        msg.first -= pop;
        write_credit_ -= 10 * pop;
        break;
      }
    }
  }

  // if we couldn't fit everything we can afford, start waiting immediately

  if (wrote < can_write) {
    socket_->WantWrite(true);
    return;
  }

  socket_->WantWrite(false);  // otherwise, nothing to write or need more credit

  // if we had anything left, see how long we need to wait to afford that

  if (!write_queue_.empty()) {
    const auto& msg = write_queue_.front();
    int cost = 10 * msg.first + msg.second;
    int debt = std::max(cost - write_credit_, 0);
    write_credit_timer_ = loop_->Delay(std::chrono::milliseconds(debt), base::borrow(&write_credit_timer_callback_));
  }
}

void Connection::WriteCreditTimer() {
  write_credit_timer_ = event::kNoTimer;
  Flush();
}

void Connection::ConnectionLost(base::error_ptr error) {
  const Config::Server& server = config_.servers(current_server_);
  const int reconnect_delay_ms = config_.reconnect_delay_ms();

  LOG(WARNING)
      << "connection to " << server << " lost (" << *error
      << ") - trying next server in " << reconnect_delay_ms << " ms";

  socket_.reset();
  if (metric_connection_up_)
    metric_connection_up_->Set(0);

  write_buffer_.clear();
  write_queue_.clear();

  if (write_credit_timer_ != event::kNoTimer) {
    loop_->CancelTimer(write_credit_timer_);
    write_credit_timer_ = event::kNoTimer;
  }

  if (auto_join_timer_ != event::kNoTimer) {
    loop_->CancelTimer(auto_join_timer_);
    auto_join_timer_ = event::kNoTimer;
  }

  if (nick_regain_timer_ != event::kNoTimer) {
    loop_->CancelTimer(nick_regain_timer_);
    nick_regain_timer_ = event::kNoTimer;
  }

  for (auto& entry : channels_) {
    if (entry.second == ChannelState::kJoined)
      readers_.Call(&Reader::ChannelLeft, entry.first);
    entry.second = ChannelState::kKnown;
  }
  if (state_ == kReady)
    readers_.Call(&Reader::ConnectionLost, config_.servers(current_server_));

  state_ = kDisconnected;
  nick_.clear();

  current_server_ = (current_server_ + 1) % config_.servers_size();

  reconnect_timer_ = loop_->Delay(std::chrono::milliseconds(reconnect_delay_ms), base::borrow(&reconnect_timer_callback_));
}

void Connection::ReconnectTimer() {
  reconnect_timer_ = event::kNoTimer;
  Start();
}

void Connection::Registered() {
  state_ = kRegistered;
  readers_.Call(&Reader::NickChanged, nick_);

  if (nick_ != config_.nick() && nick_regain_timer_ == event::kNoTimer)
    nick_regain_timer_ = loop_->Delay(kNickRegainDelay, base::borrow(&nick_regain_timer_callback_));
}

void Connection::AutoJoinTimer() {
  auto_join_timer_ = event::kNoTimer;

  if (state_ != kRegistered)
    Registered();  // just assume it's okay
  state_ = kReady;

  for (auto& entry : channels_) {
    if (entry.second == ChannelState::kKnown) {
      entry.second = ChannelState::kJoining;
      SendNow({ "JOIN", entry.first.c_str() });
    }
  }

  readers_.Call(&Reader::ConnectionReady, config_.servers(current_server_));
}

void Connection::NickRegainTimer() {
  nick_regain_timer_ = event::kNoTimer;
  if (nick_ == config_.nick())
    return;  // already okay
  SendNow({ "NICK", config_.nick().c_str() });
}

} // namespace irc
