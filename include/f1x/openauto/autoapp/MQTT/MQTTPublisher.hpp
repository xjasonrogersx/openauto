#pragma once

#include <functional>
#include <cstdint>
#include <memory>
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
  void publishNightModeState(bool active);
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

bool currentNightModeState();

void publishConnectionState(bool connected);
void publishBatteryStatus(uint32_t batteryLevel,
                          std::optional<uint32_t> timeRemainingSeconds,
                          std::optional<bool> criticalBattery);
void publishNightModeState(bool active);
void publishDebugMessage(const std::string &component,
                         const std::string &event,
                         const std::string &message);

class NightModeStateSubscriber final {
 public:
  using Handler = std::function<void(bool)>;

  explicit NightModeStateSubscriber(Handler handler);
  ~NightModeStateSubscriber();

  void start();
  void stop();

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

class MediaPlayerCommandSubscriber final {
 public:
  using Handler = std::function<void(const std::string &)>;

  explicit MediaPlayerCommandSubscriber(Handler handler);
  ~MediaPlayerCommandSubscriber();

  void start();
  void stop();

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace f1x::openauto::autoapp::mqtt
