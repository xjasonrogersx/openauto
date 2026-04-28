#!/usr/bin/env python3
"""
audio_mixer.py — DAB/Android Auto ducking mixer.

Subscribes to MQTT events from openauto and dab_tuner.
Adjusts the DAB (ffmpeg) PipeWire sink-input volume via wpctl
based on which Android Auto audio streams are active.

Configuration via environment variables:
  MQTT_HOST              MQTT broker host         (default: 127.0.0.1)
  MQTT_PORT              MQTT broker port         (default: 1883)
  MQTT_CLIENT_ID         MQTT client ID           (default: audio-mixer)
  OPENAUTO_TOPIC_PREFIX  openauto topic prefix    (default: openauto/phone)
  DAB_TOPIC              dab_tuner state topic    (default: dab/state)
  DAB_SINK_NAME          substring to match the DAB sink-input name in wpctl
                         (default: ffmpeg)
  DUCK_LEVEL             fractional volume when ducking  (default: 0.2)
"""

import json
import logging
import os
import re
import subprocess
import sys

import paho.mqtt.client as mqtt

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [audio-mixer] %(levelname)s %(message)s",
)
log = logging.getLogger(__name__)

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

MQTT_HOST = os.environ.get("MQTT_HOST", "127.0.0.1")
MQTT_PORT = int(os.environ.get("MQTT_PORT", "1883"))
MQTT_CLIENT_ID = os.environ.get("MQTT_CLIENT_ID", "audio-mixer")
OPENAUTO_TOPIC_PREFIX = os.environ.get("OPENAUTO_TOPIC_PREFIX", "openauto/phone")
DAB_TOPIC = os.environ.get("DAB_TOPIC", "dab/state")
DAB_SINK_NAME = os.environ.get("DAB_SINK_NAME", "ffmpeg")
DUCK_LEVEL = float(os.environ.get("DUCK_LEVEL", "0.2"))

OPENAUTO_DEBUG_TOPIC = f"{OPENAUTO_TOPIC_PREFIX}/debug"

# ---------------------------------------------------------------------------
# State
# ---------------------------------------------------------------------------

state = {
    "aa_media": False,
    "aa_guidance": False,
    "dab_playing": False,
}


# ---------------------------------------------------------------------------
# PipeWire volume control
# ---------------------------------------------------------------------------

def _find_dab_sink_input_id() -> str | None:
    """Return the wpctl numeric ID for the DAB sink-input, or None."""
    try:
        result = subprocess.run(
            ["wpctl", "status"],
            capture_output=True, text=True, timeout=5
        )
    except (FileNotFoundError, subprocess.TimeoutExpired) as e:
        log.error("wpctl status failed: %s", e)
        return None

    # wpctl status lines look like:
    #  │  ├─ 47. ffmpeg                          [vol: 1.00]
    for line in result.stdout.splitlines():
        if DAB_SINK_NAME.lower() in line.lower():
            match = re.search(r"\b(\d+)\.", line)
            if match:
                return match.group(1)
    log.warning("DAB sink-input '%s' not found in wpctl status", DAB_SINK_NAME)
    return None


def set_dab_volume(level: float) -> None:
    """Set DAB sink-input volume. level is 0.0–1.0."""
    sink_id = _find_dab_sink_input_id()
    if sink_id is None:
        log.warning("Cannot set DAB volume — sink-input not found")
        return
    try:
        subprocess.run(
            ["wpctl", "set-volume", sink_id, f"{level:.2f}"],
            timeout=5, check=True
        )
        log.info("DAB volume → %.0f%%", level * 100)
    except subprocess.CalledProcessError as e:
        log.error("wpctl set-volume failed: %s", e)
    except FileNotFoundError:
        log.error("wpctl not found — is PipeWire installed?")


# ---------------------------------------------------------------------------
# Ducking logic
# ---------------------------------------------------------------------------

def apply_ducking() -> None:
    """Recalculate and apply DAB volume based on current state."""
    if not state["dab_playing"]:
        # DAB is not active — nothing to do
        return

    if state["aa_media"]:
        # AA music is playing — mute DAB entirely
        set_dab_volume(0.0)
    elif state["aa_guidance"]:
        # Navigation guidance only — duck DAB
        set_dab_volume(DUCK_LEVEL)
    else:
        # No AA audio — restore DAB to full
        set_dab_volume(1.0)


# ---------------------------------------------------------------------------
# MQTT channel ID helpers
# ---------------------------------------------------------------------------

_MEDIA_CHANNEL = "MEDIA_SINK_MEDIA_AUDIO"
_GUIDANCE_CHANNEL = "MEDIA_SINK_GUIDANCE_AUDIO"


def _parse_openauto_debug(payload: str) -> None:
    """Handle a message on the openauto debug topic."""
    try:
        msg = json.loads(payload)
    except json.JSONDecodeError:
        return

    if msg.get("component") != "audio":
        return

    event = msg.get("event", "")
    message = msg.get("message", "")

    if _MEDIA_CHANNEL not in message and _GUIDANCE_CHANNEL not in message:
        return

    is_media = _MEDIA_CHANNEL in message
    is_guidance = _GUIDANCE_CHANNEL in message

    if event == "stream_started":
        if is_media:
            log.info("AA media stream started")
            state["aa_media"] = True
        if is_guidance:
            log.info("AA guidance stream started")
            state["aa_guidance"] = True
        apply_ducking()

    elif event == "stream_stopped":
        if is_media:
            log.info("AA media stream stopped")
            state["aa_media"] = False
        if is_guidance:
            log.info("AA guidance stream stopped")
            state["aa_guidance"] = False
        apply_ducking()


def _parse_dab_state(payload: str) -> None:
    """Handle a message on the dab/state topic."""
    val = payload.strip().lower()
    if val == "playing":
        log.info("DAB playing")
        state["dab_playing"] = True
    elif val == "stopped":
        log.info("DAB stopped")
        state["dab_playing"] = False
    apply_ducking()


# ---------------------------------------------------------------------------
# MQTT callbacks
# ---------------------------------------------------------------------------

def on_connect(client, userdata, flags, rc, properties=None):
    if rc == 0:
        log.info("Connected to MQTT broker %s:%d", MQTT_HOST, MQTT_PORT)
        client.subscribe(OPENAUTO_DEBUG_TOPIC)
        client.subscribe(DAB_TOPIC)
        log.info("Subscribed to %s and %s", OPENAUTO_DEBUG_TOPIC, DAB_TOPIC)
    else:
        log.error("MQTT connect failed, rc=%d", rc)


def on_disconnect(client, userdata, rc, properties=None):
    log.warning("Disconnected from MQTT broker (rc=%d), will reconnect", rc)


def on_message(client, userdata, msg):
    topic = msg.topic
    payload = msg.payload.decode("utf-8", errors="replace")
    log.debug("MQTT %s → %s", topic, payload)

    if topic == OPENAUTO_DEBUG_TOPIC:
        _parse_openauto_debug(payload)
    elif topic == DAB_TOPIC:
        _parse_dab_state(payload)


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main() -> None:
    log.info("Starting audio-mixer (DAB sink: '%s', duck level: %.0f%%)",
             DAB_SINK_NAME, DUCK_LEVEL * 100)

    client = mqtt.Client(client_id=MQTT_CLIENT_ID, protocol=mqtt.MQTTv5)
    client.on_connect = on_connect
    client.on_disconnect = on_disconnect
    client.on_message = on_message

    client.connect(MQTT_HOST, MQTT_PORT, keepalive=60)

    try:
        client.loop_forever()
    except KeyboardInterrupt:
        log.info("Shutting down")
        client.disconnect()
        sys.exit(0)


if __name__ == "__main__":
    main()
