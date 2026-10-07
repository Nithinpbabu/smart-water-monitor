# Water Tank Monitor V2 - Visual Architecture & System Flows

> [!NOTE]
> This document visualizes the complete system architecture, hardware interactions, communication protocols, state machines, and data pipelines for the **Water Tank Monitor V2** project using Mermaid diagrams.

---

## 1. High-Level System Topology & End-to-End Data Flow

The diagram below outlines the physical device layers, communication channels, and end-to-end telemetry/control paths across the **TX Node**, **RX Gateway**, and **Raspberry Pi Dashboard**.

```mermaid
graph TD
    subgraph SENSORS ["Tank & Hardware Layer"]
        US["Ultrasonic Sensor<br/>(HC-SR04 / JSN-SR04T)"]
        BAT["Battery ADC<br/>(Voltage Divider)"]
        RELAY["Motor Relay<br/>(Pumping Motor)"]
    end

    subgraph TX_NODE ["Xiao ESP32-C6 (TX Node)"]
        TX_FIRMWARE["Tx Firmware<br/>(Deep Sleep & Sampling)"]
        TX_CONFIG["NVS / RAM Config<br/>(Top/Bottom Calibration)"]
        TX_ESPNOW["ESP-NOW Radio<br/>(Channel 6 | 58:E6:C5:11:1B:10)"]
    end

    subgraph RX_NODE ["ESP32-S3 (RX Gateway Node)"]
        RX_ESPNOW["ESP-NOW Receiver<br/>(Channel 6 | 94:A9:90:03:4B:B8)"]
        RX_SAFETY["Motor Safety Engine<br/>(Overtime & Dry Run Timers)"]
        RX_SERIAL["Serial Gateway Handler"]
    end

    subgraph RPI_LAYER ["Raspberry Pi 5 Layer"]
        BACKEND["Node.js / Python Backend"]
        DB[(SQLite Database)]
        UI["Web Dashboard UI<br/>(3D Liquid Widget)"]
    end

    US -->|Raw Echo / Distance| TX_FIRMWARE
    BAT -->|Voltage Level| TX_FIRMWARE
    TX_FIRMWARE <--> TX_CONFIG
    TX_FIRMWARE -->|Water Data Struct| TX_ESPNOW

    TX_ESPNOW == ESP-NOW Wireless ==> RX_ESPNOW
    RX_ESPNOW --> RX_SAFETY
    RX_SAFETY -->|GPIO Relay Signal| RELAY
    RX_SAFETY <--> RX_SERIAL

    RX_SERIAL == USB Serial String ==> BACKEND
    BACKEND <--> DB
    BACKEND <-->|WebSockets / REST| UI
```

---

## 2. Normal Deep-Sleep Telemetry Cycle

In normal operation (when the motor is OFF), the **TX Node** sleeps to conserve solar/battery power and wakes up at the configured measurement interval (default: 5 minutes).

```mermaid
sequenceDiagram
    autonumber
    participant TX as TX Node (Xiao ESP32-C6)
    participant RX as RX Gateway (ESP32-S3)
    participant Pi as Raspberry Pi Dashboard

    Note over TX: Deep Sleep Timer Expires
    TX->>TX: 1. Wake up from Deep Sleep
    TX->>TX: 2. Trigger Ultrasonic Pulse & Read Distance
    TX->>TX: 3. Read ADC Battery Voltage
    TX->>TX: 4. Compute Water % using RAM Calibration
    
    TX->>RX: 5. ESP-NOW Send (Water %, Batt %, Motor State)
    activate RX
    RX-->>TX: 6. ESP-NOW ACK Delivery Confirmation
    deactivate RX
    
    RX->>Pi: 7. Forward Telemetry String over Serial (`LEVEL:65,BATT:88,MOTOR:OFF`)
    activate Pi
    Pi->>Pi: 8. Store in SQLite & Broadcast to Dashboard UI
    deactivate Pi

    alt Motor is OFF
        TX->>TX: 9. Re-enter Deep Sleep for 5 Minutes
    else Motor is ON / Config Mode Active
        TX->>TX: 10. Stay Awake & Enter Fast Sampling Mode
    end
```

---

## 3. High-Frequency Motor Control & Safety Filling Cycle

When the pump motor is turned ON (via manual Dashboard toggle or low water detection), the system switches to **rapid monitoring mode** (15-second interval) to prevent overflow.

```mermaid
sequenceDiagram
    autonumber
    actor User
    participant UI as Dashboard UI
    participant Pi as Raspberry Pi
    participant RX as RX Gateway (ESP32-S3)
    participant Relay as Motor Relay
    participant TX as TX Node (ESP32-C6)

    User->>UI: 1. Click "TURN MOTOR ON"
    UI->>Pi: 2. Send Motor Start Command
    Pi->>RX: 3. Serial Command: `CMD_MOTOR_ON`
    
    activate RX
    RX->>Relay: 4. Set Relay GPIO HIGH (Motor Starts)
    RX->>RX: 5. Start Max Runtime Timer (e.g., 10 mins)
    RX->>TX: 6. ESP-NOW Packet: `MOTOR_STATE_ON`
    deactivate RX

    Note over TX: TX detects Motor ON -> Disables Deep Sleep

    loop Rapid Sampling (Every 15s)
        TX->>TX: 7. Measure Water Level & Battery
        TX->>RX: 8. Send High-Freq Telemetry Packet
        RX->>Pi: 9. Relay Live Status to Dashboard
        
        alt Tank Reaches 100% (FULL)
            TX->>RX: 10. Send `EVENT_TANK_FULL`
            activate RX
            RX->>Relay: 11. Set Relay GPIO LOW (Motor OFF)
            RX->>RX: 12. Clear Runtime Timers
            RX->>TX: 13. Send `MOTOR_STATE_OFF`
            RX->>Pi: 14. Serial Alert: `STATUS:MOTOR_STOPPED_FULL`
            deactivate RX
            TX->>TX: 15. Verify Motor OFF & Re-enter Deep Sleep
        end
    end
```

---

## 4. RX Motor Safety State Machine

The **ESP32-S3 RX Gateway** acts as the primary safety lock to prevent motor burnouts, dry runs, or tank overflows even if Wi-Fi or Pi connection drops.

```mermaid
flowchart TD
    START([Power On / Reset]) --> IDLE

    subgraph IDLE["IDLE STATE"]
        MotorOff["Motor Relay OFF<br/>(Normal Monitoring Cycle)"]
    end

    IDLE -->|Command: MOTOR_ON / Auto-Fill Trigger| PUMPING

    subgraph PUMPING["PUMPING STATE (Rapid Monitoring)"]
        TimerRunning["Max Runtime Timer Running"]
        FastSampling["High-Frequency Sampling (15s)"]
        TimerRunning <--> FastSampling
    end

    PUMPING -->|Tank Full / Command: MOTOR_OFF| IDLE
    PUMPING -->|Runtime Exceeds Max Limit| FAULT_OVERTIME["FAULT: Max Runtime Exceeded"]
    PUMPING -->|No Water Level Increase| FAULT_DRY_RUN["FAULT: Dry Run Detected"]
    PUMPING -->|No TX Telemetry Signal| FAULT_COMM_LOSS["FAULT: Communication Loss"]

    FAULT_OVERTIME -->|Relay Trip LOW / Manual Reset| IDLE
    FAULT_DRY_RUN -->|Relay Trip LOW / Manual Reset| IDLE
    FAULT_COMM_LOSS -->|Relay Trip LOW / TX Reconnects| IDLE
```

---

## 5. Live Configuration Preview vs. NVS Save Flow

To adjust tank depth settings without risking persistent corruption or flash memory wear, the system supports a **RAM preview mode** before saving to NVS.

```mermaid
sequenceDiagram
    autonumber
    actor User
    participant UI as Dashboard Config Page
    participant Pi as Raspberry Pi
    participant RX as RX Gateway
    participant TX as TX Node (ESP32-C6)

    User->>UI: 1. Open Config Page & Enter New `Bottom Distance = 150 cm`
    UI->>Pi: 2. Request Live Preview Session
    Pi->>RX: 3. Serial: `CONFIG_SESSION_START`
    RX->>TX: 4. ESP-NOW: `CONFIG_SESSION_START`
    Note over TX: TX enters Awake Config Session (No Deep Sleep)

    User->>UI: 5. Click [ APPLY & PREVIEW ]
    UI->>Pi: 6. Send Temporary Config Values
    Pi->>RX: 7. Forward Temporary Values
    RX->>TX: 8. ESP-NOW Packet: `PREVIEW_CONFIG`
    
    TX->>TX: 9. Store in RAM (Do NOT write to NVS)
    TX->>TX: 10. Take Measurement using RAM Calibration
    TX->>RX: 11. Return Preview Telemetry (e.g., Water = 63%)
    RX->>Pi: 12. Forward Preview Telemetry
    Pi->>UI: 13. Update UI Circular Liquid Gauge (63%)

    alt User Clicks Save Configuration
        User->>UI: 14a. Click Save
        UI->>Pi: 15a. Command: `SAVE_CONFIG`
        Pi->>RX: 16a. Serial: `CMD_SAVE_CONFIG`
        RX->>TX: 17a. ESP-NOW: `COMMIT_CONFIG_TO_NVS`
        TX->>TX: 18a. Write RAM Config into NVS Flash
        TX-->>RX: 19a. ACK Config Saved
        RX-->>Pi: 20a. Serial: `STATUS:CONFIG_SAVED`
        Pi-->>UI: 21a. Show "Synchronized" Badge
    else User Exits / Discards
        User->>UI: 14b. Close Page without Saving
        UI->>Pi: 15b. Signal Session End
        Pi->>RX: 16b. Serial: `CONFIG_SESSION_END`
        RX->>TX: 17b. ESP-NOW: `DISCARD_PREVIEW`
        TX->>TX: 18b. Restore Last Saved NVS Config
        TX->>TX: 19b. Return to Deep Sleep Cycle
    end
```

---

## 6. Summary of Key Data Payload Schemas

```mermaid
classDiagram
    class TxTelemetryPacket {
        +uint32_t device_id
        +uint16_t water_distance_cm
        +uint8_t water_percentage
        +float battery_voltage
        +uint8_t battery_percentage
        +uint8_t motor_status
        +uint8_t config_version
    }

    class RxSafetyConfig {
        +uint16_t max_motor_runtime_sec
        +uint16_t dry_run_timeout_sec
        +uint16_t comm_loss_timeout_sec
        +uint8_t low_water_threshold
    }

    class TxNvsConfig {
        +uint16_t top_distance_cm
        +uint16_t bottom_distance_cm
        +uint16_t normal_sleep_interval_sec
        +uint16_t motor_monitoring_interval_sec
        +uint8_t config_version
    }

    TxTelemetryPacket --|> RxSafetyConfig : Telemetry Input
    RxSafetyConfig --|> TxNvsConfig : Sync Parameters
```

---

> [!TIP]
> **Key Takeaways from the Diagrams:**
> 1. **Failsafe Autonomy**: The RX node maintains direct GPIO control of the motor relay, meaning it can shut off the pump even if the Raspberry Pi backend crashes.
> 2. **Power Efficiency**: The TX node only stays fully awake during motor pumping or active configuration preview sessions.
> 3. **Non-Destructive Calibration**: `APPLY & PREVIEW` lets you calibrate your tank depth live without wearing out the ESP32 flash NVS memory.
