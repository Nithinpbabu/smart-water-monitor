"""
MQTT Listener Service for Water Tank Monitor V2 Dashboard.
Subscribes to Rx Gateway telemetry and events, logs them to SQLite,
and broadcasts updates to connected WebSocket clients.
"""

import os
import json
import time
import paho.mqtt.client as mqtt
from backend.database import (
    insert_telemetry,
    insert_motor_event,
    insert_config_state,
    insert_system_log,
)

MQTT_HOST = os.environ.get("MQTT_BROKER") or os.environ.get("MQTT_HOST") or "192.168.29.211"
MQTT_PORT = int(os.environ.get("MQTT_PORT", 1883))

latest_state = {
    "telemetry": None,
    "motor": None,
    "config": None,
    "last_seen": 0,
}

_ws_broadcast_cb = None
_client = None


def set_ws_broadcast(callback):
    global _ws_broadcast_cb
    _ws_broadcast_cb = callback


def get_mqtt_client():
    return _client


def _on_connect(client, userdata, flags, rc):
    print(f"[MQTT Listener] Connected to broker at {MQTT_HOST}:{MQTT_PORT} with code {rc}")
    client.subscribe("tank/tx01/telemetry")
    client.subscribe("tank/tx01/motor")
    client.subscribe("tank/tx01/config/state")
    client.subscribe("tank/tx01/ota/state")
    client.subscribe("tank/logs")


def _on_message(client, userdata, msg):
    global latest_state
    topic = msg.topic
    payload_str = msg.payload.decode("utf-8", errors="ignore")

    try:
        data = json.loads(payload_str)
    except Exception as e:
        print(f"[MQTT Listener] Error parsing JSON on {topic}: {e}")
        return

    latest_state["last_seen"] = time.time()

    if topic == "tank/tx01/telemetry":
        latest_state["telemetry"] = data
        insert_telemetry(data)
        if _ws_broadcast_cb:
            _ws_broadcast_cb(json.dumps({"type": "telemetry", "data": data}))

    elif topic == "tank/tx01/motor":
        latest_state["motor"] = data
        insert_motor_event(data)
        if _ws_broadcast_cb:
            _ws_broadcast_cb(json.dumps({"type": "motor", "data": data}))
            _ws_broadcast_cb(json.dumps({"type": "motor_event", "data": data}))

    elif topic == "tank/tx01/config/state":
        latest_state["config"] = data
        insert_config_state(data)
        if _ws_broadcast_cb:
            _ws_broadcast_cb(json.dumps({"type": "config", "data": data}))

    elif topic == "tank/logs":
        source = data.get("node", "rx01")
        message = data.get("message", payload_str)
        insert_system_log(source, message)
        if _ws_broadcast_cb:
            _ws_broadcast_cb(json.dumps({"type": "log", "data": data}))
            _ws_broadcast_cb(json.dumps({"type": "system_log", "data": data}))


def start_mqtt_listener():
    global _client
    _client = mqtt.Client(client_id="water_tank_dashboard_backend")
    _client.on_connect = _on_connect
    _client.on_message = _on_message

    try:
        _client.connect_async(MQTT_HOST, MQTT_PORT, 60)
        _client.loop_start()
        print(f"[MQTT Listener] Started background loop for {MQTT_HOST}:{MQTT_PORT}")
    except Exception as e:
        print(f"[MQTT Listener] Failed to start MQTT client: {e}")
