#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

namespace f1x::openauto::autoapp::mqtt {

class Publisher final {
 public:
  static Publisher &instance();

  void publishConnectionState(bool connected);
  void publishBatteryStatus(uint32_t batteryLevel,
                            std::optional<uint32_t> timeRemainingSeconds,
                            std::optional<bool> criticalBattery);
  void publishDebugMessage(const std::string &component,
                           const std::string &event,
                           const std::string &message);

 private:
  Publisher();

  void publish(const std::string &topicSuffix, const std::string &payload, bool retain);

  bool enabled_;
  std::string host_;
  std::string port_;
  std::string clientId_;
  std::string topicPrefix_;
  std::mutex mutex_;
};

void publishConnectionState(bool connected);
void publishBatteryStatus(uint32_t batteryLevel,
                          std::optional<uint32_t> timeRemainingSeconds,
                          std::optional<bool> criticalBattery);
void publishDebugMessage(const std::string &component,
                         const std::string &event,
                         const std::string &message);

}  // namespace f1x::openauto::autoapp::mqtt
