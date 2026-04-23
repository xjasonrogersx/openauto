# OpenAuto

![Banner Image](./images/banner.png)

I just want a cut down simple stand alone app that does Android auto.

---

### Description

OpenAuto is an AndroidAuto(tm) headunit emulator based on aasdk library and Qt libraries. Main goal is to run this application on the RaspberryPI 3 board computer smoothly.

### Supported functionalities

- 480p, 720p and 1080p with 30 or 60 FPS
- RaspberryPI 3 hardware acceleration support to decode video stream (up to 1080p@60!)
- Audio playback from all audio channels (Media, System and Speech)
- Audio input for voice commands
- Touchscreen and buttons input
- Bluetooth
- Automatic launch after device hotplug
- Automatic detection of connected Android devices
- Wireless (WiFi) mode via head unit server (must be enabled in hidden developer settings)
- User-friendly settings

### License

GNU GPLv3

Copyrights (c) 2018 f1x.studio (Michal Szwaj)
_AndroidAuto is registered trademark of Google Inc._

### Tmp Messages

OpenAuto and helper scripts use `/tmp` files as lightweight IPC/status flags.

| `/tmp` file                   | Written by                                                | Read by                                                                                                    | What is conveyed                                                             |
| ----------------------------- | --------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------- | -------------- |
| `/tmp/android_device`         | `packaging/opt/crankshaft/usb_action.sh`                  | `src/autoapp/UI/MainWindow.cpp`, `src/autoapp/autoapp.cpp`, `helpers/autoapp_helper`, `helpers/crankshaft` | USB Android device presence and metadata (line 1: USB path, line 2: model).  |
| `/tmp/media_playing`          | `src/autoapp/UI/MainWindow.cpp`                           | `helpers/crankshaft`, `src/autoapp/UI/MainWindow.cpp`                                                      | Local media player is active (file exists) vs stopped/paused (file removed). |
| `/tmp/entityexit`             | `src/autoapp/Service/MediaSink/VideoMediaSinkService.cpp` | `src/autoapp/UI/MainWindow.cpp`                                                                            | Request to return from Android Auto entity to the OS/home UI.                |
| `/tmp/bluetooth_pairable`     | `helpers/crankshaft`                                      | `src/autoapp/UI/MainWindow.cpp`                                                                            | Bluetooth pairing window is open (temporary 120s flag).                      |
| `/tmp/enable_pairing`         | `helpers/crankshaft`                                      | `src/autoapp/UI/MainWindow.cpp`                                                                            | Short-lived UI lock/info state while pairing mode is being enabled.          |
| `/tmp/get_outputs`            | `helpers/autoapp_helper`                                  | `src/autoapp/UI/SettingsWindow.cpp`                                                                        | Enumerated PulseAudio output devices (`[description]                         | device_name`). |
| `/tmp/get_default_output`     | `helpers/autoapp_helper`                                  | `src/autoapp/UI/SettingsWindow.cpp`                                                                        | Current/default PulseAudio output device entry.                              |
| `/tmp/get_inputs`             | `helpers/autoapp_helper`                                  | `src/autoapp/UI/SettingsWindow.cpp`                                                                        | Enumerated PulseAudio input devices (`[description]                          | device_name`). |
| `/tmp/get_default_input`      | `helpers/autoapp_helper`                                  | `src/autoapp/UI/SettingsWindow.cpp`                                                                        | Current/default PulseAudio input device entry.                               |
| `/tmp/timezone_listing`       | `helpers/autoapp_helper`                                  | `src/autoapp/UI/SettingsWindow.cpp`                                                                        | Available timezone IDs populated from `zone1970.tab`.                        |
| `/tmp/return_value`           | `helpers/autoapp_helper`                                  | `src/autoapp/UI/SettingsWindow.cpp`                                                                        | `#`-delimited system snapshot (volume, timer state text, DAC info).          |
| `/tmp/temp_recent_list`       | `helpers/autoapp_helper`                                  | `src/autoapp/UI/MainWindow.cpp`, `src/autoapp/UI/ConnectDialog.cpp`                                        | Recently seen hotspot client/IP list for wireless connection UI hints.       |
| `/tmp/custombrightness`       | `helpers/crankshaft`                                      | `src/autoapp/UI/MainWindow.cpp`, `helpers/crankshaft`                                                      | Current custom brightness level used by custom brightness command workflows. |
| `/tmp/night_mode_enabled`     | External day/night or sensor service scripts              | `src/autoapp/UI/MainWindow.cpp`, `src/autoapp/Service/Sensor/SensorService.cpp`, `helpers/crankshaft`      | Day/night mode state flag (night mode active when file exists).              |
| `/tmp/blankscreen`            | `helpers/crankshaft`                                      | `src/autoapp/UI/MainWindow.cpp`                                                                            | Full UI hidden/display-off mode indicator.                                   |
| `/tmp/screensaver`            | `helpers/crankshaft`                                      | `src/autoapp/UI/MainWindow.cpp`                                                                            | Screensaver/clock-only mode indicator.                                       |
| `/tmp/blackscreen`            | `helpers/crankshaft`                                      | `src/autoapp/UI/MainWindow.cpp`                                                                            | Force black background mode for custom command workflows.                    |
| `/tmp/shutdown`               | `src/autoapp/autoapp.cpp`                                 | External system/service handlers                                                                           | User requested system shutdown from UI.                                      |
| `/tmp/reboot`                 | `src/autoapp/autoapp.cpp`                                 | External system/service handlers                                                                           | User requested system reboot from UI.                                        |
| `/tmp/manual_hotspot_control` | `src/autoapp/UI/SettingsWindow.cpp`                       | External hotspot/network service scripts                                                                   | Manual hotspot toggle request triggered from settings UI.                    |

Notes:

- Some `/tmp` files are produced by scripts/services outside this repo (for example network/day-night integrations installed on target systems).
- Most files are treated as presence/absence flags; a smaller subset carry text payloads (`android_device`, `return_value`, audio device lists, timezone list).
