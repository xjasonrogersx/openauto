#include <f1x/openauto/autoapp/MQTT/MQTTPublisher.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <boost/asio.hpp>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <memory>
#include <sstream>
#include <thread>
#include <vector>

#include <f1x/openauto/Common/Log.hpp>

namespace f1x::openauto::autoapp::mqtt {
namespace {

std::atomic<bool> g_nightModeState{false};

std::string getEnvironmentOrDefault(const char *name, const char *defaultValue) {
  if (const char *value = std::getenv(name); value != nullptr && *value != '\0') {
    return value;
  }

  return defaultValue;
}

bool parseEnabled(const char *value) {
  if (value == nullptr || *value == '\0') {
    return true;
  }

  std::string normalized(value);
  std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

  return normalized != "0" && normalized != "false" && normalized != "no" && normalized != "off";
}

std::string escapeJson(const std::string &value) {
  std::string escaped;
  escaped.reserve(value.size());

  for (const char ch : value) {
    switch (ch) {
      case '\\':
        escaped += "\\\\";
        break;
      case '"':
        escaped += "\\\"";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        escaped += ch;
        break;
    }
  }

  return escaped;
}

void appendString(std::vector<uint8_t> &buffer, const std::string &value) {
  const auto size = static_cast<uint16_t>(value.size());
  buffer.push_back(static_cast<uint8_t>((size >> 8) & 0xff));
  buffer.push_back(static_cast<uint8_t>(size & 0xff));
  buffer.insert(buffer.end(), value.begin(), value.end());
}

void appendRemainingLength(std::vector<uint8_t> &buffer, std::size_t length) {
  do {
    uint8_t byte = static_cast<uint8_t>(length % 128);
    length /= 128;
    if (length > 0) {
      byte |= 0x80;
    }
    buffer.push_back(byte);
  } while (length > 0);
}

std::vector<uint8_t> buildConnectPacket(const std::string &clientId) {
  std::vector<uint8_t> variableHeader;
  appendString(variableHeader, "MQTT");
  variableHeader.push_back(0x04);
  variableHeader.push_back(0x02);
  variableHeader.push_back(0x00);
  variableHeader.push_back(0x1e);

  std::vector<uint8_t> payload;
  appendString(payload, clientId);

  std::vector<uint8_t> packet;
  packet.push_back(0x10);
  appendRemainingLength(packet, variableHeader.size() + payload.size());
  packet.insert(packet.end(), variableHeader.begin(), variableHeader.end());
  packet.insert(packet.end(), payload.begin(), payload.end());
  return packet;
}

std::vector<uint8_t> buildPublishPacket(const std::string &topic, const std::string &payload, bool retain) {
  std::vector<uint8_t> variableHeader;
  appendString(variableHeader, topic);

  std::vector<uint8_t> packet;
  packet.push_back(static_cast<uint8_t>(0x30 | (retain ? 0x01 : 0x00)));
  appendRemainingLength(packet, variableHeader.size() + payload.size());
  packet.insert(packet.end(), variableHeader.begin(), variableHeader.end());
  packet.insert(packet.end(), payload.begin(), payload.end());
  return packet;
}

std::vector<uint8_t> buildSubscribePacket(uint16_t packetId, const std::string &topic) {
  std::vector<uint8_t> variableHeader;
  variableHeader.push_back(static_cast<uint8_t>((packetId >> 8) & 0xff));
  variableHeader.push_back(static_cast<uint8_t>(packetId & 0xff));

  std::vector<uint8_t> payload;
  appendString(payload, topic);
  payload.push_back(0x00);

  std::vector<uint8_t> packet;
  packet.push_back(0x82);
  appendRemainingLength(packet, variableHeader.size() + payload.size());
  packet.insert(packet.end(), variableHeader.begin(), variableHeader.end());
  packet.insert(packet.end(), payload.begin(), payload.end());
  return packet;
}

std::vector<uint8_t> buildPubAckPacket(uint16_t packetId) {
  return {0x40, 0x02, static_cast<uint8_t>((packetId >> 8) & 0xff), static_cast<uint8_t>(packetId & 0xff)};
}

std::string trimAndLower(const std::string &value) {
  const auto begin = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
    return std::isspace(ch) != 0;
  });

  if (begin == value.end()) {
    return "";
  }

  const auto end = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
    return std::isspace(ch) != 0;
  }).base();

  std::string normalized(begin, end);
  std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return normalized;
}

std::string removeWhitespace(const std::string &value) {
  std::string normalized;
  normalized.reserve(value.size());

  for (const char ch : value) {
    if (std::isspace(static_cast<unsigned char>(ch)) == 0) {
      normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
  }

  return normalized;
}

bool tryParseNightModePayload(const std::string &payload, bool &active) {
  const std::string normalized = trimAndLower(payload);
  if (normalized == "1" || normalized == "true" || normalized == "on" || normalized == "night" ||
      normalized == "enabled") {
    active = true;
    return true;
  }

  if (normalized == "0" || normalized == "false" || normalized == "off" || normalized == "day" ||
      normalized == "disabled") {
    active = false;
    return true;
  }

  const std::string compact = removeWhitespace(payload);
  if (compact.find("\"active\":true") != std::string::npos ||
      compact.find("\"night_mode\":true") != std::string::npos ||
      compact.find("\"mode\":\"night\"") != std::string::npos) {
    active = true;
    return true;
  }

  if (compact.find("\"active\":false") != std::string::npos ||
      compact.find("\"night_mode\":false") != std::string::npos ||
      compact.find("\"mode\":\"day\"") != std::string::npos) {
    active = false;
    return true;
  }

  return false;
}

std::size_t readRemainingLength(boost::asio::ip::tcp::socket &socket) {
  std::size_t multiplier = 1;
  std::size_t value = 0;
  uint8_t encodedByte = 0;

  do {
    boost::asio::read(socket, boost::asio::buffer(&encodedByte, 1));
    value += static_cast<std::size_t>(encodedByte & 127) * multiplier;
    multiplier *= 128;
  } while ((encodedByte & 128) != 0);

  return value;
}

bool readPacket(boost::asio::ip::tcp::socket &socket, uint8_t &header, std::vector<uint8_t> &payload) {
  boost::asio::read(socket, boost::asio::buffer(&header, 1));
  const std::size_t remainingLength = readRemainingLength(socket);
  payload.assign(remainingLength, 0);

  if (remainingLength > 0) {
    boost::asio::read(socket, boost::asio::buffer(payload.data(), payload.size()));
  }

  return true;
}

}  // namespace

Publisher &Publisher::instance() {
  static Publisher publisher;
  return publisher;
}

Publisher::Publisher()
    : enabled_(parseEnabled(std::getenv("OPENAUTO_MQTT_ENABLED"))),
      host_(getEnvironmentOrDefault("OPENAUTO_MQTT_HOST", "127.0.0.1")),
      port_(getEnvironmentOrDefault("OPENAUTO_MQTT_PORT", "1883")),
      clientId_(getEnvironmentOrDefault("OPENAUTO_MQTT_CLIENT_ID", "openauto")),
      topicPrefix_(getEnvironmentOrDefault("OPENAUTO_MQTT_TOPIC_PREFIX", "openauto/phone")) {
  OPENAUTO_LOG(info) << "[MQTT] Publisher " << (enabled_ ? "enabled" : "disabled")
                     << " for broker " << host_ << ':' << port_ << " topic prefix " << topicPrefix_;
}

void Publisher::publishConnectionState(bool connected) {
  std::ostringstream payload;
  payload << "{\"connected\":" << (connected ? "true" : "false") << ",\"source\":\"android_auto\"}";
  publish("status", payload.str(), true);
}

void Publisher::publishBatteryStatus(uint32_t batteryLevel,
                                     std::optional<uint32_t> timeRemainingSeconds,
                                     std::optional<bool> criticalBattery) {
  std::ostringstream payload;
  payload << "{\"level\":" << batteryLevel;

  if (timeRemainingSeconds.has_value()) {
    payload << ",\"time_remaining_s\":" << *timeRemainingSeconds;
  }

  if (criticalBattery.has_value()) {
    payload << ",\"critical\":" << (*criticalBattery ? "true" : "false");
  }

  payload << '}';
  publish("battery", payload.str(), true);
}

void Publisher::publishNightModeState(bool active) {
  g_nightModeState.store(active, std::memory_order_release);
  std::ostringstream payload;
  payload << "{\"active\":" << (active ? "true" : "false")
          << ",\"mode\":\"" << (active ? "night" : "day")
          << "\",\"source\":\"android_auto\"}";
  publish("night_mode", payload.str(), true);
}

bool currentNightModeState() {
  return g_nightModeState.load(std::memory_order_acquire);
}

void Publisher::publishDebugMessage(const std::string &component,
                                    const std::string &event,
                                    const std::string &message) {
  std::ostringstream payload;
  payload << "{\"component\":\"" << escapeJson(component)
          << "\",\"event\":\"" << escapeJson(event)
          << "\",\"message\":\"" << escapeJson(message)
          << "\",\"source\":\"android_auto\"}";
  publish("debug", payload.str(), false);
}

void Publisher::publish(const std::string &topicSuffix, const std::string &payload, bool retain) {
  if (!enabled_) {
    return;
  }

  const std::lock_guard<std::mutex> lock(mutex_);
  const std::string topic = topicPrefix_ + "/" + topicSuffix;

  try {
    boost::asio::io_service ioService;
    boost::asio::ip::tcp::resolver resolver(ioService);
    boost::asio::ip::tcp::socket socket(ioService);

    auto endpoints = resolver.resolve(host_, port_);
    boost::asio::connect(socket, endpoints);

    const auto connectPacket = buildConnectPacket(clientId_);
    boost::asio::write(socket, boost::asio::buffer(connectPacket));

    std::array<uint8_t, 4> connack{};
    boost::asio::read(socket, boost::asio::buffer(connack));

    if (connack[0] != 0x20 || connack[1] != 0x02 || connack[3] != 0x00) {
      OPENAUTO_LOG(warning) << "[MQTT] Broker rejected connection for topic " << topic;
      return;
    }

    const auto publishPacket = buildPublishPacket(topic, payload, retain);
    boost::asio::write(socket, boost::asio::buffer(publishPacket));

    const std::array<uint8_t, 2> disconnectPacket{{0xe0, 0x00}};
    boost::asio::write(socket, boost::asio::buffer(disconnectPacket));
  } catch (const std::exception &e) {
    OPENAUTO_LOG(warning) << "[MQTT] Publish failed for topic " << topic << ": " << e.what();
  }
}

void publishConnectionState(bool connected) {
  Publisher::instance().publishConnectionState(connected);
}

void publishBatteryStatus(uint32_t batteryLevel,
                          std::optional<uint32_t> timeRemainingSeconds,
                          std::optional<bool> criticalBattery) {
  Publisher::instance().publishBatteryStatus(batteryLevel, timeRemainingSeconds, criticalBattery);
}

void publishNightModeState(bool active) {
  Publisher::instance().publishNightModeState(active);
}

void publishDebugMessage(const std::string &component,
                         const std::string &event,
                         const std::string &message) {
  Publisher::instance().publishDebugMessage(component, event, message);
}

class NightModeStateSubscriber::Impl {
 public:
  explicit Impl(Handler handler)
      : enabled_(parseEnabled(std::getenv("OPENAUTO_MQTT_ENABLED"))),
        host_(getEnvironmentOrDefault("OPENAUTO_MQTT_HOST", "127.0.0.1")),
        port_(getEnvironmentOrDefault("OPENAUTO_MQTT_PORT", "1883")),
        clientId_(getEnvironmentOrDefault("OPENAUTO_MQTT_CLIENT_ID", "openauto")),
        topicPrefix_(getEnvironmentOrDefault("OPENAUTO_MQTT_TOPIC_PREFIX", "openauto/phone")),
        handler_(std::move(handler)) {}

  ~Impl() {
    stop();
  }

  void start() {
    if (!enabled_ || worker_.joinable()) {
      return;
    }

    stopRequested_.store(false, std::memory_order_release);
    worker_ = std::thread([this]() { run(); });
  }

  void stop() {
    stopRequested_.store(true, std::memory_order_release);
    closeSocket();

    if (worker_.joinable()) {
      worker_.join();
    }
  }

 private:
  void run() {
    if (!enabled_) {
      return;
    }

    const std::string topic = topicPrefix_ + "/night_mode";

    while (!stopRequested_.load(std::memory_order_acquire)) {
      try {
        boost::asio::io_service ioService;
        boost::asio::ip::tcp::resolver resolver(ioService);
        auto socket = std::make_shared<boost::asio::ip::tcp::socket>(ioService);

        {
          const std::lock_guard<std::mutex> lock(socketMutex_);
          socket_ = socket;
        }

        auto endpoints = resolver.resolve(host_, port_);
        boost::asio::connect(*socket, endpoints);

        boost::asio::write(*socket, boost::asio::buffer(buildConnectPacket(clientId_ + "-night-mode")));

        uint8_t header = 0;
        std::vector<uint8_t> payload;
        readPacket(*socket, header, payload);
        if (header != 0x20 || payload.size() != 2 || payload[1] != 0x00) {
          OPENAUTO_LOG(warning) << "[MQTT] Night mode state subscriber connection rejected.";
          closeSocket();
          waitBeforeReconnect();
          continue;
        }

        constexpr uint16_t subscribePacketId = 1;
        const auto subscribePacket = buildSubscribePacket(subscribePacketId, topic);
        boost::asio::write(*socket, boost::asio::buffer(subscribePacket));

        readPacket(*socket, header, payload);
        if (header != 0x90 || payload.size() < 3 || payload[2] == 0x80) {
          OPENAUTO_LOG(warning) << "[MQTT] Night mode subscribe failed for topic " << topic;
          closeSocket();
          waitBeforeReconnect();
          continue;
        }

        OPENAUTO_LOG(info) << "[MQTT] Subscribed to night mode state on " << topic;

        while (!stopRequested_.load(std::memory_order_acquire)) {
          readPacket(*socket, header, payload);
          const uint8_t packetType = header & 0xf0;
          if (packetType != 0x30) {
            continue;
          }

          if (payload.size() < 2) {
            continue;
          }

          const std::size_t topicLength = (static_cast<std::size_t>(payload[0]) << 8) | payload[1];
          if (payload.size() < 2 + topicLength) {
            continue;
          }

          const std::string messageTopic(payload.begin() + 2, payload.begin() + 2 + topicLength);
          std::size_t offset = 2 + topicLength;
          const uint8_t qos = static_cast<uint8_t>((header >> 1) & 0x03);
          uint16_t packetId = 0;

          if (qos > 0) {
            if (payload.size() < offset + 2) {
              continue;
            }

            packetId = static_cast<uint16_t>((payload[offset] << 8) | payload[offset + 1]);
            offset += 2;
          }

          const std::string message(payload.begin() + static_cast<std::ptrdiff_t>(offset), payload.end());
          if (messageTopic == topic) {
            bool active = false;
            if (tryParseNightModePayload(message, active)) {
              g_nightModeState.store(active, std::memory_order_release);
              handler_(active);
            } else {
              OPENAUTO_LOG(warning) << "[MQTT] Ignoring invalid night mode payload: " << message;
            }
          }

          if (qos == 1) {
            const auto pubAckPacket = buildPubAckPacket(packetId);
            boost::asio::write(*socket, boost::asio::buffer(pubAckPacket));
          }
        }
      } catch (const std::exception &e) {
        if (!stopRequested_.load(std::memory_order_acquire)) {
          OPENAUTO_LOG(warning) << "[MQTT] Night mode state subscriber disconnected: " << e.what();
          waitBeforeReconnect();
        }
      }

      closeSocket();
    }
  }

  void waitBeforeReconnect() {
    for (int attempt = 0; attempt < 50 && !stopRequested_.load(std::memory_order_acquire); ++attempt) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }

  void closeSocket() {
    const std::lock_guard<std::mutex> lock(socketMutex_);
    if (socket_ != nullptr) {
      boost::system::error_code ec;
      socket_->close(ec);
      socket_.reset();
    }
  }

  bool enabled_;
  std::string host_;
  std::string port_;
  std::string clientId_;
  std::string topicPrefix_;
  Handler handler_;
  std::atomic<bool> stopRequested_{false};
  std::thread worker_;
  std::mutex socketMutex_;
  std::shared_ptr<boost::asio::ip::tcp::socket> socket_;
};

NightModeStateSubscriber::NightModeStateSubscriber(Handler handler)
    : impl_(std::make_unique<Impl>(std::move(handler))) {}

NightModeStateSubscriber::~NightModeStateSubscriber() = default;

void NightModeStateSubscriber::start() {
  impl_->start();
}

void NightModeStateSubscriber::stop() {
  impl_->stop();
}

}  // namespace f1x::openauto::autoapp::mqtt
