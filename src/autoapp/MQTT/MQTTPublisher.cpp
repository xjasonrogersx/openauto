#include <f1x/openauto/autoapp/MQTT/MQTTPublisher.hpp>

#include <algorithm>
#include <array>
#include <boost/asio.hpp>
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <vector>

#include <f1x/openauto/Common/Log.hpp>

namespace f1x::openauto::autoapp::mqtt {
namespace {

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

void publishDebugMessage(const std::string &component,
                         const std::string &event,
                         const std::string &message) {
  Publisher::instance().publishDebugMessage(component, event, message);
}

}  // namespace f1x::openauto::autoapp::mqtt
