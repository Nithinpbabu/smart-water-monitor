# 💧 Water Tank Monitor 

> **A production-deployed, three-layer IoT system** for real-time water tank monitoring, automated motor control, and historical consumption analytics — built on a 500 L rooftop tank.

[![Arduino](https://img.shields.io/badge/Firmware-Arduino%20C%2B%2B-00979D?logo=arduino)](Tx/)
[![ESP-NOW](https://img.shields.io/badge/Protocol-ESP--NOW-blue)](Tx/protocol.h)
[![Python](https://img.shields.io/badge/Backend-Python%20%2F%20FastAPI-3776AB?logo=python)](Rpi-Dashboard/backend/)
[![Docker](https://img.shields.io/badge/Deploy-Docker%20Compose-2496ED?logo=docker)](Rpi-Dashboard/docker-compose.yml)
[![SQLite](https://img.shields.io/badge/Database-SQLite%20WAL-07405E?logo=sqlite)](Rpi-Dashboard/backend/database.py)
[![WebSocket](https://img.shields.io/badge/Realtime-WebSockets-brightgreen)](Rpi-Dashboard/backend/app.py)
[![Platform](https://img.shields.io/badge/Host-Raspberry%20Pi%205-C51A4A?logo=raspberrypi)](Rpi-Dashboard/)

---

## Overview

Water Tank Monitor is a **fully autonomous, local-network IoT system** deployed on a real rooftop water tank. It continuously measures water level, controls the pump motor, and serves a live dashboard — all without any cloud dependency.

The system is architectured across **three hardware layers**:

| Layer | Hardware | Role |
|---|---|---|
| **TX Node** | Xiao ESP32-C6 | Solar + Battery-powered sensor node — deep sleep, ultrasonics, ESP-NOW |
| **RX Gateway** | ESP32-S3 | Mains-powered relay controller & Wi-Fi/ESP-NOW bridge |
| **Dashboard** | Raspberry Pi 5 | FastAPI backend, SQLite storage, WebSocket UI |

```mermaid
graph TD
    subgraph SENSORS ["Tank & Hardware Layer"]
        US["Ultrasonic Sensor<br/>(JSN-SR04T)"]
        BAT["Battery ADC<br/>(Voltage Divider)"]
        RELAY["Motor Relay<br/>(Water Pump)"]
    end

    subgraph TX_NODE ["Xiao ESP32-C6 · TX Node"]
        TX_FIRMWARE["Firmware<br/>(Deep Sleep & Sampling)"]
        TX_CONFIG["NVS / RAM Config<br/>(Top/Bottom Calibration)"]
        TX_ESPNOW["ESP-NOW Radio<br/>(Channel 6)"]
    end

    subgraph RX_NODE ["ESP32-S3 · RX Gateway"]
        RX_ESPNOW["ESP-NOW Receiver"]
        RX_SAFETY["Motor Safety Engine<br/>(Overtime · Dry-Run · Comm-Loss)"]
        RX_MQTT["MQTT Publisher"]
    end

    subgraph RPI_LAYER ["Raspberry Pi 5"]
        BACKEND["FastAPI Backend"]
        DB[("SQLite WAL")]
        UI["Web Dashboard<br/>(3D Liquid Gauge · WebSocket)"]
    end

    US --> TX_FIRMWARE
    BAT --> TX_FIRMWARE
    TX_FIRMWARE <--> TX_CONFIG
    TX_FIRMWARE --> TX_ESPNOW

    TX_ESPNOW == "ESP-NOW (2.4 GHz)" ==> RX_ESPNOW
    RX_ESPNOW --> RX_SAFETY
    RX_SAFETY --> RELAY
    RX_SAFETY --> RX_MQTT

    RX_MQTT == "MQTT / Wi-Fi" ==> BACKEND
    BACKEND <--> DB
    BACKEND <-->|"WebSockets / REST"| UI
```

---

## Dashboard Screenshots

<div align="center">

| Live Gauge | Home Screen | Water History |
|:---:|:---:|:---:|
| <img src="docs/screenshots/dashboard_gauge.png" width="160"/> | <img src="docs/screenshots/dashboard_home.png" width="160"/> | <img src="docs/screenshots/dashboard_water_history.png" width="160"/> |

| Battery Health | Consumption Analytics |
|:---:|:---:|
| <img src="docs/screenshots/dashboard_battery.png" width="160"/> | <img src="docs/screenshots/dashboard_consumption.png" width="160"/> |

</div>

---

## Key Features

### ⚡ Power Optimization (TX Node)

| Technique | Detail |
|---|---|
| **ESP32 Deep Sleep** | TX sleeps between readings; wakes only to sample + transmit (~2–3 s active per cycle) |
| **Adaptive Duty Cycle** | Normal: 5-min sleep interval · Motor filling: 15-s rapid-poll (no sleep) |
| **ESP-NOW over Wi-Fi** | ESP-NOW skips Wi-Fi association handshake entirely — transmit completes in ~6 ms |
| **RTC Memory Mode Persistence** | Operating mode (`NORMAL` / `MOTOR_FILLING` / `CONFIG_SESSION` / `OTA`) stored in RTC RAM, survives deep sleep resets |
| **Battery Life** | Calculated theoretical runtime: **~30 days** on a 500 mAh Li-ion at 5-min interval. Real-world deployment confirmed this estimate was accurate. |

### 🛡️ Motor Safety Engine (RX Node)

The RX gateway holds **direct GPIO control** of the relay — the motor is cut off by hardware even if the Raspberry Pi backend is down.

Three independent fault triggers:

| Fault | Condition | Action |
|---|---|---|
| **Max Runtime** | Motor on longer than configured limit | Relay tripped OFF immediately |
| **Dry Run** | No water-level increase over timeout period | Relay tripped OFF immediately |
| **Comm Loss** | No telemetry received from TX for timeout period | Relay tripped OFF immediately |

### 🗓️ Pre-Peak Fill Scheduler

The RX node runs a **NTP-synced daily schedule** (`pre_peak_fill.cpp`) that automatically triggers the pump before a configurable peak-usage window (default: 17:30 IST). Once triggered, completion is written to NVS flash — a power cut between trigger and fill does not cause a double-fill.

### 🔧 Non-Destructive Live Config Preview

Tank calibration (`WATER_TOP_LVL` / `WATER_BOTTOM_LVL`) can be tested live without writing to flash:

1. Dashboard opens a Config Session → TX stays awake (no deep sleep)
2. User edits values → clicks **Apply & Preview** → values stored in RAM only
3. TX immediately measures using preview calibration and reports live `%`
4. User clicks **Save** → values committed to NVS; or closes the page → TX discards preview and restores last saved config

This prevents NVS flash wear from speculative calibration updates.

### 📡 Bidirectional ESP-NOW Protocol

A typed binary protocol (`protocol.h`) with 18 message types and application-level ACKs:

| Packet | Purpose |
|---|---|
| `SensorPacket` | TX → RX telemetry (water %, battery, motor state, sequence) |
| `AckPacket` | RX → TX delivery confirmation + authoritative motor state |
| `ConfigPacket` | RX → TX config delivery (preview or save) |
| `MotorCommandPacket` | RX → TX motor ON/OFF with max runtime enforcement |
| `FullDetectedPacket` | TX → RX tank-full event (triggers relay OFF) |

### 🔄 Wireless OTA Firmware Updates

TX firmware can be updated over-the-air via Wi-Fi. OTA mode is entered via an ESP-NOW command from RX and persisted in RTC RAM so the TX correctly resumes OTA state after the required ESP.restart() mid-update.

---

## Normal Telemetry Cycle

```mermaid
sequenceDiagram
    autonumber
    participant TX as TX Node (Xiao ESP32-C6)
    participant RX as RX Gateway (ESP32-S3)
    participant Pi as Raspberry Pi Dashboard

    Note over TX: Deep Sleep Timer Expires
    TX->>TX: 1. Wake from Deep Sleep
    TX->>TX: 2. Ultrasonic pulse → distance measurement
    TX->>TX: 3. ADC battery voltage reading
    TX->>TX: 4. Compute Water % (RAM calibration)

    TX->>RX: 5. ESP-NOW SensorPacket (Water %, Batt %, Motor State)
    activate RX
    RX-->>TX: 6. AckPacket (sequence echo + authoritative motor state)
    deactivate RX

    RX->>Pi: 7. MQTT publish (level, battery, motor status)
    activate Pi
    Pi->>Pi: 8. SQLite WAL insert → WebSocket broadcast to UI
    deactivate Pi

    alt Motor OFF
        TX->>TX: 9. Re-enter Deep Sleep (5 min)
    else Motor ON or Config Session
        TX->>TX: 10. Stay awake → fast-poll loop (15 s)
    end
```

---

## Motor Safety State Machine

```mermaid
flowchart TD
    START([Power On / Reset]) --> IDLE

    subgraph IDLE["IDLE STATE"]
        MotorOff["Motor Relay OFF<br/>(Normal 5-min monitoring cycle)"]
    end

    IDLE -->|"CMD: MOTOR_ON / Auto-Fill / Pre-Peak Schedule"| PUMPING

    subgraph PUMPING["PUMPING STATE (Rapid 15s Monitoring)"]
        TimerRunning["Max Runtime Timer Running"]
        FastSampling["High-Frequency TX Telemetry (15s)"]
        TimerRunning <--> FastSampling
    end

    PUMPING -->|"Tank Full / CMD: MOTOR_OFF"| IDLE
    PUMPING -->|"Runtime > Max Limit"| FAULT_OVERTIME["🔴 FAULT: Max Runtime Exceeded"]
    PUMPING -->|"No Water Level Increase"| FAULT_DRY_RUN["🔴 FAULT: Dry Run Detected"]
    PUMPING -->|"No TX Signal"| FAULT_COMM["🔴 FAULT: Communication Loss"]

    FAULT_OVERTIME -->|"Relay tripped LOW · Manual Reset"| IDLE
    FAULT_DRY_RUN -->|"Relay tripped LOW · Manual Reset"| IDLE
    FAULT_COMM -->|"Relay tripped LOW · TX Reconnects"| IDLE
```

---

## High-Frequency Filling Cycle

```mermaid
sequenceDiagram
    autonumber
    actor User
    participant UI as Dashboard UI
    participant Pi as Raspberry Pi
    participant RX as RX Gateway (ESP32-S3)
    participant Relay as Motor Relay
    participant TX as TX Node (ESP32-C6)

    User->>UI: 1. Toggle Motor ON
    UI->>Pi: 2. Motor start command
    Pi->>RX: 3. MQTT: CMD_MOTOR_ON

    activate RX
    RX->>Relay: 4. GPIO HIGH → Motor starts
    RX->>RX: 5. Start Max Runtime Timer
    RX->>TX: 6. ESP-NOW: MOTOR_STATE_ON
    deactivate RX

    Note over TX: Motor ON detected → Disable Deep Sleep

    loop Rapid Sampling (Every 15 s)
        TX->>TX: 7. Measure water level + battery
        TX->>RX: 8. SensorPacket (high-freq)
        RX->>Pi: 9. MQTT relay to dashboard

        alt Tank ≥ 100% (FULL)
            TX->>RX: 10. FullDetectedPacket
            activate RX
            RX->>Relay: 11. GPIO LOW → Motor OFF
            RX->>RX: 12. Clear runtime timers
            RX->>TX: 13. AckPacket (motor=OFF)
            RX->>Pi: 14. MQTT: STATUS=MOTOR_STOPPED_FULL
            deactivate RX
            TX->>TX: 15. Verify OFF → Re-enter Deep Sleep
        end
    end
```

---

## Firmware Architecture

### TX Node — `Tx/`

```
Tx/
├── Tx.ino                # Main state machine, boot & loop orchestration
├── protocol.h            # Shared typed binary packet definitions (18 msg types)
├── motor_state.cpp       # RTC-persisted operating mode FSM
│                         #   NORMAL → MOTOR_FILLING → CONFIG_SESSION → OTA
├── espnow_tx.cpp         # ESP-NOW with sequence tracking, ACK & 10-retry logic
├── config.cpp            # NVS saved config + RAM preview separation
├── water_sensor.cpp      # JSN-SR04T ultrasonic measurement + glitch filtering
├── battery.cpp           # ADC voltage → calibrated % curve
├── sleep_manager.cpp     # Deep sleep orchestration + boot count (RTC)
├── ota_manager.cpp       # Wi-Fi OTA with RTC mode persistence across ESP.restart()
└── antenna_select.cpp    # External / internal antenna switching (Xiao ESP32-C6)
```

**Design highlights:**
- Operating mode persisted in `RTC_DATA_ATTR` — survives both deep sleep and OTA resets
- Low-water + ACK-fail triggers a 15-second short-sleep retry (max 3 attempts) before falling back to normal interval — prevents missing a low-water fill event due to a transient radio glitch
- Sensor glitch correction: measurements deviating >3× from the rolling average are flagged `SENSOR_STATUS_GLITCH_FIX` and smoothed

### RX Gateway — `Rx/`

```
Rx/
├── Rx.ino                # Main loop & telemetry routing
├── protocol.h            # Same shared protocol header as TX
├── espnow_gateway.cpp    # Bidirectional ESP-NOW gateway + per-device ACK tables
├── motor_controller.cpp  # GPIO relay control, runtime enforcement, fault logic
├── pre_peak_fill.cpp     # Configurable daily pre-fill scheduler (NVS-persisted)
├── mqtt_client.cpp       # MQTT publishing → Raspberry Pi broker
├── serial_cli.cpp        # Serial command interface for debugging & diagnostics
└── wifi_manager.cpp      # NTP-synced Wi-Fi (ESP-NOW & Wi-Fi on same channel)
```

**Design highlights:**
- ESP-NOW and Wi-Fi **coexist on the same 2.4 GHz radio**, pinned to the same channel as the home router — no radio-switching delays
- Motor relay is controlled by direct GPIO — independent of network state; Pi crash cannot leave motor running
- Pre-peak fill NVS-persists the "already done today" date as `YYYYMMDD` integer — a power cut between schedule trigger and fill will not re-trigger on next boot

---

## Dashboard — `Rpi-Dashboard/`

**Stack:** FastAPI · MQTT · SQLite (WAL) · WebSockets · Docker

### Three Pages

| Page | What it shows |
|---|---|
| **Home** | 3D animated liquid gauge · Motor toggle (live state) · Battery % · Config version · Last telemetry time |
| **Graphs** | Water level % history · Water consumed (L) · Motor runtime · Battery voltage — ranges: 1H → 1Y |
| **Config** | Live RAM-preview calibration session · Measurement interval · Motor safety thresholds · Save/Discard flow |

### Analytics

Water consumption is calculated per fill cycle:

```
Consumed (L) = ΔWater% × 500 L / 100
```

The dashboard tracks fill cycles, total daily consumption, and motor runtime across configurable time windows (1D / 7D / 30D / 1Y).

### CLI Management Tools

```bash
# View DB stats + configured tank capacity
./manage_db --status

# Change tank capacity to 500 L (live — no restart needed)
./manage_db --tank 500

# Clear last 6 hours of telemetry
./clear_data --6h

# Clear data older than 30 days
./manage_db --older-than 30d

# Skip confirmation prompt
./clear_data --6h -y
```

Inside the Docker container on Pi:
```bash
docker exec -it water-tank-dashboard ./manage_db --status
```

### Deployment

```bash
cd /home/pi/water_tank_v2/Rpi-Dashboard

# Build and start (single command)
docker compose up -d

# Access dashboard
http://<RASPBERRY_PI_IP>:8001
```

---

## Wire Protocol — Packet Schemas

```mermaid
classDiagram
    class SensorPacket {
        +uint8_t  messageType
        +uint32_t sequence
        +uint32_t configVersion
        +uint16_t waterDistanceCm
        +uint8_t  waterPercentage
        +uint16_t batteryVoltageMv
        +uint8_t  batteryPercentage
        +uint8_t  sensorStatus
        +uint8_t  motorStatus
    }

    class AckPacket {
        +uint8_t  messageType
        +uint32_t sequence
        +uint32_t configVersion
        +uint8_t  rxMotorStatus
    }

    class ConfigPacket {
        +uint8_t  messageType
        +uint32_t configVersion
        +uint16_t waterTopLevelCm
        +uint16_t waterBottomLevelCm
        +uint32_t normalSleepIntervalSeconds
        +uint32_t motorMonitoringIntervalSeconds
        +uint8_t  lowWaterThreshold
        +uint32_t maxMotorRuntimeSeconds
    }

    class MotorCommandPacket {
        +uint8_t  messageType
        +uint32_t sequence
        +uint32_t maxMotorRuntimeSeconds
    }

    class FullDetectedPacket {
        +uint8_t  messageType
        +uint32_t sequence
        +uint16_t waterDistanceCm
        +uint8_t  waterPercentage
    }

    SensorPacket --|> AckPacket : triggers
    AckPacket --|> ConfigPacket : may carry config version delta
    MotorCommandPacket --|> SensorPacket : governs rapid-poll mode
    SensorPacket --|> FullDetectedPacket : escalates when full
```

All packets use `#pragma pack(push, 1)` — zero padding, exact wire size.

---

## Repository Layout

```
water_tank_v2/
├── Tx/                                  # Xiao ESP32-C6 Arduino firmware
│   ├── Tx.ino
│   ├── protocol.h                       # Shared packet definitions
│   └── *.cpp / *.h                      # Modular subsystems
│
├── Rx/                                  # ESP32-S3 Arduino firmware
│   ├── Rx.ino
│   ├── protocol.h                       # Same shared protocol
│   └── *.cpp / *.h
│
├── Rpi-Dashboard/                       # Raspberry Pi service
│   ├── backend/
│   │   ├── app.py                       # FastAPI + WebSocket server
│   │   ├── database.py                  # SQLite WAL layer + analytics queries
│   │   └── mqtt_listener.py            # MQTT → DB ingestion
│   ├── static/                          # Dashboard HTML/CSS/JS
│   ├── manage_db                        # CLI: status, capacity, purge
│   ├── clear_data                       # CLI: time-range purge shorthand
│   ├── Dockerfile
│   └── docker-compose.yml
│
├── docs/
│   └── screenshots/                     # Dashboard screenshots
│
├── visual_system_flow.md               # Full Mermaid architecture diagrams
└── water_tank_monitor_v2_architecture.md  # Complete design specification
```

---

## Quick Start

### 1. Flash TX Firmware (Xiao ESP32-C6)
Open `Tx/Tx.ino` in Arduino IDE. Set your RX MAC address and flash.

### 2. Flash RX Firmware (ESP32-S3)
Open `Rx/Rx.ino` in Arduino IDE. Set your Wi-Fi credentials and flash.

### 3. Deploy Dashboard (Raspberry Pi 5)
```bash
git clone <this-repo>
cd water_tank_v2/Rpi-Dashboard
docker compose up -d
```

Dashboard available at `http://<PI_IP>:8001`.

---

## Further Reading

- [`visual_system_flow.md`](visual_system_flow.md) — Complete Mermaid diagrams for all system flows, state machines, and config session sequences
- [`water_tank_monitor_v2_architecture.md`](water_tank_monitor_v2_architecture.md) — Full design specification and engineering decisions
- [`Rpi-Dashboard/README.md`](Rpi-Dashboard/README.md) — Dashboard-specific deployment and CLI reference
