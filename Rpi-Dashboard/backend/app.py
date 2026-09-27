"""
FastAPI Application for Water Tank Monitor V2 Dashboard.
Serves the static frontend, provides REST API for historical data,
WebSocket endpoint for live telemetry streaming, and MQTT command publishing.
"""

import os
import json
import time
import asyncio
from typing import Set
from contextlib import asynccontextmanager

from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.staticfiles import StaticFiles
from fastapi.responses import FileResponse, JSONResponse

from backend.database import (
    init_db,
    get_telemetry_history,
    get_motor_events_history,
    get_motor_runtime_data,
    get_water_analytics_data,
    get_latest_config,
    get_system_logs,
)
from backend.mqtt_listener import (
    start_mqtt_listener,
    set_ws_broadcast,
    latest_state,
    get_mqtt_client,
)

# ─── Connected WebSocket Clients ───
connected_clients: Set[WebSocket] = set()
broadcast_loop: asyncio.AbstractEventLoop = None


def ws_broadcast_sync(message: str):
    """Thread-safe broadcast from MQTT listener thread to async WebSocket clients."""
    if broadcast_loop and connected_clients:
        asyncio.run_coroutine_threadsafe(_broadcast(message), broadcast_loop)


async def _broadcast(message: str):
    """Send message to all connected WebSocket clients."""
    dead = set()
    for ws in connected_clients:
        try:
            await ws.send_text(message)
        except Exception:
            dead.add(ws)
    connected_clients.difference_update(dead)


# ─── App Lifecycle ───
@asynccontextmanager
async def lifespan(app: FastAPI):
    global broadcast_loop
    broadcast_loop = asyncio.get_event_loop()

    # Initialize database
    init_db()

    # Register WebSocket broadcast callback and start MQTT
    set_ws_broadcast(ws_broadcast_sync)
    start_mqtt_listener()

    print("[App] Water Tank Dashboard backend started on port 8001")
    yield
    print("[App] Shutting down...")


app = FastAPI(title="Water Tank Monitor V2 Dashboard", lifespan=lifespan)

# ─── Static Files ───
STATIC_DIR = os.path.join(os.path.dirname(os.path.dirname(__file__)), "static")
app.mount("/static", StaticFiles(directory=STATIC_DIR), name="static")


@app.get("/")
async def index():
    return FileResponse(os.path.join(STATIC_DIR, "index.html"))


# ─── REST API Endpoints ───

@app.get("/api/state")
async def get_current_state():
    """Return the latest known system state."""
    return JSONResponse(latest_state)


@app.get("/api/telemetry")
async def api_telemetry(hours: float = 24):
    """Return telemetry history for the last N hours."""
    data = get_telemetry_history(hours=hours)
    return JSONResponse(data)


@app.get("/api/motor-events")
async def api_motor_events(hours: float = 168):
    """Return motor events for the last N hours (default 7 days)."""
    data = get_motor_events_history(hours=hours)
    return JSONResponse(data)


@app.get("/api/motor-runtime")
async def api_motor_runtime(hours: float = 24):
    """Return motor runtime buckets and last active timestamp for requested hours."""
    data = get_motor_runtime_data(hours=hours)
    return JSONResponse(data)


@app.get("/api/water-analytics")
async def api_water_analytics(hours: float = 24):
    """Return water level history and water consumption analytics (in Liters)."""
    data = get_water_analytics_data(hours=hours)
    return JSONResponse(data)


@app.get("/api/config")
async def api_config():
    """Return the latest config state."""
    data = get_latest_config()
    return JSONResponse(data)


@app.get("/api/logs")
async def api_logs(limit: int = 200):
    """Return recent system log entries."""
    data = get_system_logs(limit=limit)
    return JSONResponse(data)


# ─── WebSocket Live Telemetry ───

@app.websocket("/ws")
async def websocket_endpoint(ws: WebSocket):
    await ws.accept()
    connected_clients.add(ws)
    print(f"[WS] Client connected. Total: {len(connected_clients)}")

    # Send current state immediately on connect
    try:
        await ws.send_text(json.dumps({
            "type": "initial_state",
            "data": latest_state,
        }))
    except Exception:
        pass

    try:
        while True:
            raw = await ws.receive_text()
            try:
                msg = json.loads(raw)
                await handle_ws_command(msg)
            except json.JSONDecodeError:
                pass
    except WebSocketDisconnect:
        pass
    finally:
        connected_clients.discard(ws)
        print(f"[WS] Client disconnected. Total: {len(connected_clients)}")


async def handle_ws_command(msg: dict):
    """Handle commands from the frontend via WebSocket."""
    cmd_type = msg.get("type", "")
    data = msg.get("data", {})

    mqtt_client = get_mqtt_client()
    if not mqtt_client:
        print("[WS] No MQTT client available")
        return

    if cmd_type == "motor_command":
        # Publish motor ON/OFF command to Rx Gateway
        action = data.get("action", "OFF")
        payload = json.dumps({"command": f"MOTOR_{action}"})
        mqtt_client.publish("tank/tx01/command", payload)
        print(f"[WS->MQTT] Motor command: {action}")

    elif cmd_type == "config_set":
        # Publish config update to Rx Gateway
        payload = json.dumps(data)
        mqtt_client.publish("tank/tx01/config/set", payload)
        print(f"[WS->MQTT] Config update: {data}")

    elif cmd_type == "ota_start":
        mqtt_client.publish("tank/tx01/ota/start", "OTA_START")
        print("[WS->MQTT] OTA Start command sent")

    elif cmd_type == "ota_exit":
        mqtt_client.publish("tank/tx01/ota/cmd", "OTA_EXIT")
        print("[WS->MQTT] OTA Exit command sent")

    elif cmd_type == "prepeak_config":
        payload = json.dumps(data)
        mqtt_client.publish("tank/tx01/prepeak/set", payload)
        print(f"[WS->MQTT] Pre-Peak config: {data}")
