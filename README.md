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

| file| Writtenby| Read by| What is conveyed|
| -- | -- | -- | -- | 
| `/tmp/android_device` | `packaging/opt/crankshaft/usb_action.sh`  | `src/autoapp/UI/MainWindow.cpp`, `src/autoapp/autoapp.cpp`, `helpers/autoapp_helper`, `helpers/crankshaft` | USB Android device presence and metadata (line 1: USB path, line 2: model).  |
| `/tmp/media_playing`  | `src/autoapp/UI/MainWindow.cpp`   | `helpers/crankshaft`, `src/autoapp/UI/MainWindow.cpp`  | Local media player is active (file exists) vs stopped/paused (file removed). |
| `/tmp/entityexit` | `src/autoapp/Service/MediaSink/VideoMediaSinkService.cpp` | `src/autoapp/UI/MainWindow.cpp`| Request to return from Android Auto entity to the OS/home UI.|
| `/tmp/bluetooth_pairable` | `helpers/crankshaft`  | `src/autoapp/UI/MainWindow.cpp`| Bluetooth pairing window is open (temporary 120s flag).  |
| `/tmp/enable_pairing` | `helpers/crankshaft`  | `src/autoapp/UI/MainWindow.cpp`| Short-lived UI lock/info state while pairing mode is being enabled.  |
| `/tmp/get_outputs`| `helpers/autoapp_helper`  | `src/autoapp/UI/SettingsWindow.cpp`| Enumerated PulseAudio output devices (`[description] | device_name`). |
| `/tmp/get_default_output` | `helpers/autoapp_helper`  | `src/autoapp/UI/SettingsWindow.cpp`| Current/default PulseAudio output device entry.  |
| `/tmp/get_inputs` | `helpers/autoapp_helper`  | `src/autoapp/UI/SettingsWindow.cpp`| Enumerated PulseAudio input devices (`[description]  | device_name`). |
| `/tmp/get_default_input`  | `helpers/autoapp_helper`  | `src/autoapp/UI/SettingsWindow.cpp`| Current/default PulseAudio input device entry.   |
| `/tmp/timezone_listing`   | `helpers/autoapp_helper`  | `src/autoapp/UI/SettingsWindow.cpp`| Available timezone IDs populated from `zone1970.tab`.|
| `/tmp/return_value`   | `helpers/autoapp_helper`  | `src/autoapp/UI/SettingsWindow.cpp`| `#`-delimited system snapshot (volume, timer state text, DAC info).  |
| `/tmp/temp_recent_list`   | `helpers/autoapp_helper`  | `src/autoapp/UI/MainWindow.cpp`, `src/autoapp/UI/ConnectDialog.cpp`| Recently seen hotspot client/IP list for wireless connection UI hints.   |
| `/tmp/custombrightness`   | `helpers/crankshaft`  | `src/autoapp/UI/MainWindow.cpp`, `helpers/crankshaft`  | Current custom brightness level used by custom brightness command workflows. |
| `/tmp/blankscreen`| `helpers/crankshaft`  | `src/autoapp/UI/MainWindow.cpp`| Full UI hidden/display-off mode indicator.   |
| `/tmp/screensaver`| `helpers/crankshaft`  | `src/autoapp/UI/MainWindow.cpp`| Screensaver/clock-only mode indicator.   |
| `/tmp/blackscreen`| `helpers/crankshaft`  | `src/autoapp/UI/MainWindow.cpp`| Force black background mode for custom command workflows.|
| `/tmp/shutdown`   | `src/autoapp/autoapp.cpp` | External system/service handlers   | User requested system shutdown from UI.   |
| `/tmp/reboot` | `src/autoapp/autoapp.cpp` | External system/service handlers   | User requested system reboot from UI.|
| `/tmp/manual_hotspot_control` | `src/autoapp/UI/SettingsWindow.cpp`   | External hotspot/network service scripts   | Manual hotspot toggle request triggered from settings UI.|

Notes:

- Some `/tmp` files are produced by scripts/services outside this repo (for example network/day-night integrations installed on target systems).
- Most files are treated as presence/absence flags; a smaller subset carry text payloads (`android_device`, `return_value`, audio device lists, timezone list).


## Audio handling

Android Auto sends up to three distinct PCM audio streams over separate aasdk channels, each handled by its own service instance:

| Stream | Service Class | Channel ID | Format |
|---|---|---|---|
| **Media/Music** | `MediaAudioService` | `MEDIA_SINK_MEDIA_AUDIO` | Stereo, 16-bit, 48000 Hz |
| **Guidance (nav)** | `GuidanceAudioService` | `MEDIA_SINK_GUIDANCE_AUDIO` | Mono, 16-bit, 16000 Hz |
| **System audio** | `SystemAudioService` | `MEDIA_SINK_SYSTEM_AUDIO` | Mono, 16-bit, 16000 Hz |

Telephony audio is present in code but commented out — suspected to be handled via Bluetooth in practice.

### Data path

```
Android Phone (USB)
    │  PCM over aasdk messenger channels
    ▼
AudioMediaSinkService  (one instance per stream)
    │  onMediaWithTimestampIndication()
    │  → audioOutput_->write(timestamp, buffer)
    ▼
IAudioOutput  (RtAudioOutput or QtAudioOutput)
    │  writes raw PCM into SequentialBuffer (QIODevice ring buffer)
    ▼
RtAudio callback: audioBufferReadHandler()
    │  pulls frames from SequentialBuffer into outputBuffer
    ▼
RtAudio backend
    │  prefers LINUX_PULSE (PulseAudio) if compiled in, else default (ALSA)
    ▼
ALSA / PulseAudio  →  hardware DAC
```

### Key details

- **Backend selection**: `RtAudioOutput` prefers PulseAudio (`LINUX_PULSE`) if compiled in, otherwise falls back to the RtAudio default (ALSA on Pi). Configurable via `AudioOutputBackendType` — can be switched to the Qt audio backend (`QtAudioOutput`) in settings.
- **Stream options**: `RTAUDIO_MINIMIZE_LATENCY | RTAUDIO_SCHEDULE_REALTIME` — real-time scheduling priority is requested for low latency.
- **Buffer sizes**: 1024 frames for 16 kHz streams, 2048 frames for the 48 kHz media stream (tuned to match observed AA packet sizes).
- **Flow control**: After each PCM packet is consumed, `AudioMediaSinkService` sends a `MediaAck` (ack=1) back to the phone, gating the phone's send rate.

### PipeWire on Raspberry Pi OS Bookworm

Bookworm uses **PipeWire** as the audio server (not PulseAudio). `RtAudioOutput` still selects the `LINUX_PULSE` backend when compiled in — it connects cleanly to PipeWire's PulseAudio-compatible socket. If only bare PipeWire is installed (no `pipewire-pulse`), RtAudio falls back to ALSA via PipeWire's ALSA emulation.

The `pactl` volume calls in `autoapp_helper` require `pipewire-pulse` to be installed. Without it, volume control from the UI is silently broken. Install with:

```bash
sudo apt install pipewire-pulse
systemctl --user enable --now pipewire-pulse
```

### DAB Tuner audio ducking

Android handles audio focus between its own apps (i.e. music automatically ducks for navigation guidance). However, an external source such as a DAB radio tuner is invisible to Android — the head unit must manage ducking itself.

#### Architecture

Each process publishes its state to MQTT. A dedicated `audio-mixer` process subscribes and adjusts PipeWire volumes using `wpctl`:

```
openauto  ──MQTT──→  audio-mixer  ←──MQTT──  dab_tuner
                          │
                     wpctl set-volume
                    (per sink-input)
```

This keeps openauto and dab_tuner decoupled from each other. Ducking logic lives in one place and is easy to tune.

#### MQTT topics

openauto publishes audio stream events to its debug topic. The mixer subscribes to:

| Topic | Publisher | Relevant payload fields |
|---|---|---|
| `openauto/phone/debug` | openauto | `component="audio"`, `event="stream_started"\|"stream_stopped"`, `message` contains `channel=MEDIA_SINK_MEDIA_AUDIO\|MEDIA_SINK_GUIDANCE_AUDIO` |
| `dab/state` | dab_tuner | `playing` / `stopped` |


Example messages seen:
```
openauto/phone/debug {"component":"audio","event":"stream_started","message":"channel=MEDIA_SINK_GUIDANCE_AUDIO, session=0","source":"android_auto"}
openauto/phone/debug {"component":"audio","event":"stream_stopped","message":"channel=MEDIA_SINK_GUIDANCE_AUDIO, session=0","source":"android_auto"}
openauto/phone/debug {"component":"audio","event":"stream_stopped","message":"channel=MEDIA_SINK_MEDIA_AUDIO, session=0","source":"android_auto"}
openauto/phone/debug {"component":"audio","event":"stream_started","message":"channel=MEDIA_SINK_MEDIA_AUDIO, session=1","source":"android_auto"}
openauto/phone/debug {"component":"audio","event":"stream_stopped","message":"channel=MEDIA_SINK_MEDIA_AUDIO, session=1","source":"android_auto"}
openauto/phone/debug {"component":"audio","event":"stream_started","message":"channel=MEDIA_SINK_GUIDANCE_AUDIO, session=1","source":"android_auto"}
openauto/phone/debug {"component":"audio","event":"stream_stopped","message":"channel=MEDIA_SINK_GUIDANCE_AUDIO, session=1","source":"android_auto"}
openauto/phone/debug {"component":"audio","event":"stream_started","message":"channel=MEDIA_SINK_GUIDANCE_AUDIO, session=2","source":"android_auto"}
openauto/phone/debug {"component":"audio","event":"stream_stopped","message":"channel=MEDIA_SINK_GUIDANCE_AUDIO, session=2","source":"android_auto"}
```

#### Ducking rules

| Condition | DAB volume |
|---|---|
| AA media stream active | 0% (mute) |
| AA guidance stream active (no media) | 20% (duck) |
| AA guidance stream active (media also active) | 0% (stay muted) |
| Neither AA stream active | 100% |


#### Microphone handling

OpenAuto captures microphone audio locally on the head unit and sends it to the phone over Android Auto's microphone media-source channel.

Data path:

```
Microphone (USB / HAT / onboard input)
    │  PCM capture (mono, 16-bit, 16 kHz)
    ▼
QtAudioInput (QAudioInput)
    │  onReadyRead() chunks
    ▼
MediaSourceService / MicrophoneMediaSourceService
    │  sendMediaSourceWithTimestampIndication()
    ▼
Android phone (Assistant / voice recognition)
```

Operational behavior:

- If Android requests microphone open, OpenAuto starts local capture and streams PCM frames.
- If no input device is available (or open fails), OpenAuto returns an internal microphone error to Android.
- There is no local fallback microphone engine in OpenAuto itself. Any fallback behavior (for example using the handset mic) is controlled by the phone/Assistant side.

#### Hotword and voice trigger

- Hotword detection (for example, "Hey Google") is performed on the phone, not by OpenAuto.
- A voice-command button/key can also be used to trigger voice input manually.
- Pressing the button is optional in normal operation when phone-side hotword triggering is available.


#### Mixer script

See `audio_mixer/audio_mixer.py`. Run as a systemd service alongside openauto and dab_tuner.

Important environment knobs:

- `DAB_SINK_NAME`: substring used to find the DAB sink-input in `wpctl status` (default `ffmpeg`)
- `DUCK_LEVEL`: ducked DAB volume for guidance-only state (default `0.2` = 20%)
- `OPENAUTO_TOPIC_PREFIX`: base topic prefix (default `openauto/phone`)

```ini
# /etc/systemd/system/audio-mixer.service
[Unit]
Description=Audio Mixer / Ducking Service
After=network.target pipewire.service

[Service]
ExecStart=/usr/bin/python3 /home/pi/openauto/audio_mixer/audio_mixer.py
Restart=on-failure
Environment=MQTT_HOST=127.0.0.1
Environment=DAB_SINK_NAME=ffmpeg
Environment=DUCK_LEVEL=0.20
Environment=OPENAUTO_TOPIC_PREFIX=openauto/phone

[Install]
WantedBy=multi-user.target
```


## night_mode

OpenAuto now treats MQTT as the in-process source of truth for day/night mode. The `night_mode` topic is retained so OpenAuto can recover the current state after reconnect or restart without relying on `/tmp/night_mode_enabled`.

### Topic

- Topic: `openauto/phone/night_mode`
- Retained: `true`
- QoS: `1` recommended

### Accepted payloads

OpenAuto accepts either a small JSON payload or a simple text value.

JSON examples:

```json
{"active":true}
{"active":false}
{"mode":"night"}
{"mode":"day"}
```

Text examples:

```text
night
day
true
false
on
off
1
0
```

### mosquitto_pub examples

Set night mode on:

```bash
mosquitto_pub -h 127.0.0.1 -p 1883 -q 1 -r -t openauto/phone/night_mode -m '{"active":true}'
```

Set night mode off:

```bash
mosquitto_pub -h 127.0.0.1 -p 1883 -q 1 -r -t openauto/phone/night_mode -m '{"active":false}'
```

Equivalent plain-text commands:

```bash
mosquitto_pub -h 127.0.0.1 -p 1883 -q 1 -r -t openauto/phone/night_mode -m night
mosquitto_pub -h 127.0.0.1 -p 1883 -q 1 -r -t openauto/phone/night_mode -m day
```

Inspect the currently retained state:

```bash
mosquitto_sub -h 127.0.0.1 -p 1883 -t openauto/phone/night_mode -C 1 -v
```

### Behavior

- OpenAuto publishes retained state updates to `openauto/phone/night_mode` when the UI day/night buttons are used.
- OpenAuto subscribes to the same retained topic and applies incoming state updates to its UI and Android Auto sensor state.
- External integrations such as a headlight or sunset controller should publish the effective mode directly to the retained topic.

## Media controls

OpenAuto supports MQTT-based media controls so external integrations (for example steering-wheel button bridges) can trigger Android app playback through Android Auto key events.

### Topics

- Command topic: `openauto/phone/media_player/command`
- Command retained: `false` (required to avoid stale command replay)
- Command QoS: `1` recommended

### Command payloads

Accepted command payloads:

- Plain text: `play`, `start`, `stop`, `pause`, `next`, `prev`, `toggle`
- JSON: `{"action":"play"}` (same action values as above)

`start` is treated as an alias for `play`.

### mosquitto_pub examples

Start playback:

```bash
mosquitto_pub -h 127.0.0.1 -p 1883 -q 1 -r false -t openauto/phone/media_player/command -m '{"action":"play"}'
```

Stop playback:

```bash
mosquitto_pub -h 127.0.0.1 -p 1883 -q 1 -r false -t openauto/phone/media_player/command -m '{"action":"stop"}'
```

Simple text command examples:

```bash
mosquitto_pub -h 127.0.0.1 -p 1883 -q 1 -t openauto/phone/media_player/command -m start
mosquitto_pub -h 127.0.0.1 -p 1883 -q 1 -t openauto/phone/media_player/command -m stop
```

Optional navigation commands:

```bash
mosquitto_pub -h 127.0.0.1 -p 1883 -q 1 -t openauto/phone/media_player/command -m next
mosquitto_pub -h 127.0.0.1 -p 1883 -q 1 -t openauto/phone/media_player/command -m prev
```

