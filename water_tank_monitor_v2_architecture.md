# Water Tank Monitor V2
## Full System Architecture, Communication Protocol, Firmware Design, Dashboard Design, Motor Control, and Implementation Plan

**Project:** Solar/Battery Powered Water Tank Monitor  
**Architecture:** Xiao ESP32-C3 TX + ESP32-S3 RX Gateway + Raspberry Pi 5 Dashboard  
**Document status:** Architecture baseline / implementation specification  
**Priority:** Reliability first, power efficiency second, security later

---

# 1. Project Overview

The Water Tank Monitor V2 is a three-layer local IoT system with automatic motor control.

```text
                         LOCAL NETWORK
                 ┌─────────────────────────┐
                 │      Raspberry Pi 5     │
                 │                         │
                 │ Dashboard               │
                 │ Backend/API             │
                 │ MQTT Broker             │
                 │ Database                │
                 └────────────┬────────────┘
                              │
                            Wi-Fi
                              │
                              ▼
                 ┌─────────────────────────┐
                 │       ESP32-S3 RX       │
                 │                         │
                 │ Wi-Fi Gateway           │
                 │ ESP-NOW Gateway         │
                 │ Motor/Relay Control     │
                 │ Config Cache            │
                 └────────────┬────────────┘
                              │
                           ESP-NOW
                              │
                              ▼
                 ┌─────────────────────────┐
                 │     Xiao ESP32-C3 TX    │
                 │                         │
                 │ Ultrasonic Sensor       │
                 │ Battery / Solar         │
                 │ Deep Sleep              │
                 │ Tank/Motor Monitoring   │
                 └─────────────────────────┘
                              │
                              ▼
                           WATER TANK
                              ▲
                              │
                         Water Pump
                              ▲
                              │
                         Relay Module
```

The Xiao ESP32-C3 TX is the battery/solar-powered sensor node.

The ESP32-S3 RX is the mains-powered gateway and motor-control node.

The Raspberry Pi 5 hosts the dashboard, stores historical data, and manages configuration.

The core telemetry path is:

```text
TX sensor
   ↓
ESP-NOW
   ↓
RX ESP32-S3
   ↓
Wi-Fi / MQTT
   ↓
Raspberry Pi 5
   ↓
Dashboard
```

The configuration path is bidirectional:

```text
Dashboard
   ↓
Raspberry Pi
   ↓
Wi-Fi / MQTT
   ↓
RX ESP32-S3
   ↓
ESP-NOW
   ↓
TX ESP32-C3
   ↓
NVS
```

The motor-control feedback path is:

```text
RX
   ↓
Relay
   ↓
Motor
   ↓
Tank fills
   ↓
TX measures water level
   ↓
TX reports FULL
   ↓
RX turns relay OFF
```

---

# 2. Design Goals

## Primary goals

1. Measure water tank level reliably.
2. Operate TX from battery + solar power.
3. Use ESP32-C3 deep sleep to minimize TX power consumption.
4. Use ESP-NOW for TX ↔ RX communication.
5. Use ESP32-S3 as a Wi-Fi + ESP-NOW gateway.
6. Keep the RX gateway available for both Wi-Fi and ESP-NOW.
7. Allow configuration from the Raspberry Pi dashboard.
8. Automatically control the water pump/motor through the RX relay.
9. Prevent tank overflow by stopping the motor when the tank is full.
10. Keep TX in normal deep sleep when the motor is OFF.
11. Use a faster monitoring cycle while the motor is filling the tank.
12. Maintain reliable communication using ACKs and retries.
13. Preserve the existing, real-world-tested sensor and battery measurement logic.
14. Maintain a fail-safe maximum motor runtime.
15. Allow future expansion to richer monitoring and multiple devices.

## Secondary goals

- Historical water-level graphs.
- Battery monitoring.
- TX online/offline detection.
- Configurable measurement interval.
- Configurable tank calibration.
- Sensor fault detection.
- Motor state monitoring.
- Motor runtime tracking.
- Low-water detection.
- Automatic filling.
- Future alerts and automation.
- Future multi-tank support.

## Current non-priority

Security is intentionally not the initial focus.

ESP-NOW encryption, stronger authentication, and other security hardening can be added later.

---

# 3. Core Architectural Decision

## RX should NOT repeatedly switch between ESP-NOW and Wi-Fi

The RX should not use this architecture:

```text
TX sends
↓
TX sleeps
↓
RX disconnects ESP-NOW
↓
RX connects Wi-Fi
↓
RX publishes data
↓
RX disconnects Wi-Fi
↓
RX reconnects ESP-NOW
↓
wait for TX
```

Instead, the RX should operate as a permanent gateway:

```text
                 ESP32-S3 RX
              ┌───────────────┐
              │               │
              │     Wi-Fi     │────── Raspberry Pi
              │               │
              │   ESP-NOW     │────── TX
              │               │
              │ Motor Control │────── Relay
              │               │
              └───────────────┘
```

Wi-Fi and ESP-NOW should coexist on the RX.

## Important radio requirement

ESP-NOW and Wi-Fi use the same 2.4 GHz radio.

Therefore:

**ESP-NOW and RX Wi-Fi must operate on the same Wi-Fi channel.**

The home router/AP should preferably use a fixed 2.4 GHz channel rather than dynamically changing channels.

Example:

```text
Router/AP
   ↓
2.4 GHz Channel 6

RX Wi-Fi
   ↓
Channel 6

RX ESP-NOW
   ↓
Channel 6

TX ESP-NOW
   ↓
Channel 6
```

The TX does not need to connect to Wi-Fi.

---

# 4. System Components

## 4.1 TX: Xiao ESP32-C3

Power:

- Battery powered.
- Solar charging/power support.
- Deep sleep between measurements.

Responsibilities:

- Wake from deep sleep.
- Power ultrasonic sensor.
- Measure water distance.
- Calculate water percentage.
- Read battery voltage.
- Calculate battery percentage.
- Send telemetry using ESP-NOW.
- Use ESP-NOW delivery status for normal telemetry where sufficient.
- Use application-level ACKs for important control/configuration commands.
- Receive configuration if needed.
- Support a temporary configuration session initiated by the Raspberry Pi dashboard.
- Remain awake during an active configuration session.
- Apply preview configuration in RAM without writing it to NVS.
- Store configuration in NVS only after explicit Save Configuration.
- Check motor state.
- If motor is OFF, return to normal deep sleep.
- If motor is ON, enter motor-monitoring mode and remain fully awake.
- Do NOT enter ESP32 deep sleep while the motor is ON and the tank is being filled.
- During motor-monitoring mode, measure the tank at the configured motor-monitoring interval.
- Send frequent tank-level updates while the tank is filling.
- Detect the configured full level.
- Send a FULL_DETECTED event when the tank reaches `water_percentage >= 100%`.
- RX turns the motor OFF and sends/confirm `MOTOR_OFF` to TX.
- Remain awake until both conditions are true:
  1. `water_percentage >= 100%`
  2. `motor_status == OFF`
- Only after both conditions are satisfied may TX return to normal deep-sleep operation.

The TX should remain as simple and energy-efficient as possible.

The TX should **not** directly control the mains motor relay.

The TX is primarily the battery-powered sensor and tank-level feedback node.

---

## 4.2 RX: ESP32-S3

Power:

- Main power.

Responsibilities:

- Maintain Wi-Fi connection.
- Maintain ESP-NOW communication with TX.
- Receive telemetry.
- Process ESP-NOW delivery/application acknowledgments as appropriate.
- Forward telemetry to Raspberry Pi.
- Receive configuration and configuration-session commands from Raspberry Pi.
- Store pending/saved configuration state.
- Manage temporary configuration sessions.
- Send configuration to TX at its next communication window.
- Track TX communication state.
- Handle retries at the gateway level where appropriate.
- Maintain motor state.
- Control the motor relay.
- Send Motor ON/OFF status to TX.
- Receive FULL_DETECTED from TX.
- Turn the motor OFF when the tank is full.
- Enforce maximum motor runtime.
- Report motor status to Raspberry Pi.

The RX is a gateway and real-time motor-control node, not the primary database or dashboard.

---

## 4.3 Raspberry Pi 5

Responsibilities:

- Host dashboard.
- Store telemetry/history.
- Run MQTT broker.
- Run backend/API.
- Manage configuration.
- Display current tank level.
- Display historical water level.
- Display battery.
- Display TX health/status.
- Display motor state.
- Display motor runtime/history.
- Configure tank parameters.
- Configure normal measurement interval.
- Configure motor-monitoring interval.
- Show configuration synchronization status.

Suggested logical components:

```text
Raspberry Pi 5
│
├── Dashboard
│
├── Backend/API
│
├── MQTT Broker
│
└── SQLite database
```

Docker can be used for these services.

---
# 4.4 ESP-NOW Device MAC Addresses

The following MAC addresses are the fixed hardware identifiers used for ESP-NOW communication.

## TX - Xiao ESP32-C6
```cpp 
uint8_t TX_MAC[] = {0x58, 0xE6, 0xC5, 0x11, 0x1B, 0x10};
```
## RX - ESP32-S3
```cpp
uint8_t RX_MAC[] = {0x94, 0xA9, 0x90, 0x03, 0x4B, 0xB8};
```
---

# 5. Raspberry Pi Dashboard

The dashboard should have exactly **three primary pages**:

```text
1. Landing Page
2. Graph Page
3. Config Page
```

Navigation:

```text
┌──────────────────────────────────────────────┐
│ Water Tank Monitor                           │
│                                              │
│ [ Home ]   [ Graph ]   [ Config ]            │
└──────────────────────────────────────────────┘
```

---

# 6. Dashboard Page 1: Landing Page
## Landing Page Water-Level Visualization

The Landing Page must use the existing custom circular liquid progress widget for displaying the current water percentage.

The widget is already designed and implemented separately in:

```text
 liquid-progress-widget.html
```
The landing page is the main live-status page, compatable with Mobile and desktop screens
Rest of the webpage pages also should follow similar colour pallet, aestetics (with Black BG) , 3D liquid -glass syle buttons 

It should answer:

> What is happening with the tank right now?

Suggested layout:

```text
┌──────────────────────────────────────┐
│                                      │
│             ┌─────────┐              │
│             │         │              │
│             │   63%   │              │
│             │  LIQUID │              │
│             │ PROGRESS│              │
│             │         │              │
│             └─────────┘              │
│                                      │
│          Water Level: 63%            │
│                                      │
│             Motor: OFF               │
│ Battery: 88%       TX: ONLINE        │
│                                      │
│                                      │
│ Last Update: 10:45:03                │
│ Config Version: 12                   │
└──────────────────────────────────────┘
```

## Landing page information

At minimum:

- Current water percentage (main center of attention).
- Motor ON/OFF state + Dynamic button manually turning ON/OFF motor (button isteslf shows staus - Motot On / Motor Off)
- Battery percentage.
- TX online/offline state.
- Last telemetry time. ()
- Current configuration version.

## Motor display

Example:

```text
Motor: OFF
```

or:

```text
Motor: ON
Filling tank
Started: 10:21
Runtime: 04:32
```

If the TX reports full:

```text
Tank FULL
Motor stopping...
```

Then:

```text
Tank FULL
Motor OFF
```

---

# 7. Dashboard Page 2: Graph Page

The Graph page displays historical measurements.

Primary graph:

```text
Water Level (%)
100 ┤                 ╭──
 80 ┤       ╭─────────╯
 60 ┤───────╯
 40 ┤
 20 ┤
  0 ┤
    └────────────────────────
      06   09   12   15   18
```

Possible graph ranges:

- Last hour.
- Last 12 hours.
- Last 24 hours.
- Last 7 days.
- Last 30 days.
- last 6 months

## Graph data

Store and graph:

- Water percentage.
- No.of min motor was turned on 
- Battery percentage.
- water comsumed in Liters (calculated based on % of water, by taking into consideraation 100% = XX L of water)



The initial Graph page can prioritize water consumed and battery.

---

# 8. Dashboard Page 3: Config Page

The Config page controls runtime parameters.

Initial configuration:

```text
Water Tank Settings

Tank Top Distance:
[ 10 ] cm

Tank Bottom Distance:
[ 150 ] cm

Normal Measurement Interval:
[ 5 ] minutes

Motor Monitoring Interval:
[ 15 ] seconds

Low Water Threshold:
[ 5 ] %

Maximum Motor Runtime:
[ 10 ] minutes

Current Config Version:
12

TX Config Version:
11

Status:
Waiting for TX

[ APPLY & PREVIEW ]    [ SAVE CONFIGURATION ]

Last configured: dd/mm, hh:mm am/pm
(small text)
```

### Config page live-preview behavior

The Config page should operate as a temporary **configuration session**.

When the Config page is opened, the Raspberry Pi should request a configuration session through the RX. The TX then enters configuration mode and remains awake instead of returning to deep sleep.

```text
Dashboard Config Page opened
        ↓
Raspberry Pi
        ↓
MQTT / Wi-Fi
        ↓
RX
        ↓
CONFIG_SESSION_START
        ↓
TX
        ↓
ACK
        ↓
TX remains awake
```

During this session, the TX can provide live water-level readings to the dashboard with low delay.

The normal battery-saving deep-sleep behavior is therefore:

```text
Normal operation:
measure → transmit → deep sleep
```

while configuration mode is:

```text
Config session:
measure → transmit → wait → measure → transmit → ...
```

The TX should remain awake only while an active configuration session exists.

### Temporary configuration vs saved configuration

The TX should maintain a clear distinction between:

```text
Saved Configuration
    ↓
Stored permanently in NVS

Temporary Configuration
    ↓
Held only in RAM during the configuration session
```

Example:

```text
Saved:
WATER_TOP_LVL    = 20 cm
WATER_BOTTOM_LVL = 100 cm

User edits:
WATER_BOTTOM_LVL = 150 cm
```

The edited value must **not** be written to NVS merely because the user changed the field or pressed `APPLY & PREVIEW`.

When the user presses:

```text
[ APPLY & PREVIEW ]
```

the temporary configuration is sent to the TX.

The TX:

```text
1. Validates the configuration.
2. Applies it to the active RAM configuration.
3. Does NOT write it to NVS.
4. Measures using the temporary values.
5. Returns live telemetry.
```

The dashboard can then immediately show the resulting water percentage.

### Live water-percentage preview

The live preview is primarily required for:

```text
WATER_TOP_LVL
WATER_BOTTOM_LVL
```

For example:

```text
Previous:
WATER_BOTTOM_LVL = 100 cm

Temporary:
WATER_BOTTOM_LVL = 150 cm
```

After `APPLY & PREVIEW`, the TX uses the temporary calibration and reports the resulting water percentage.

The Config page should display the live result, for example:

```text
Temporary Configuration

Top Level:
20 cm

Bottom Level:
150 cm

Live Water Level:
63%

Status:
Previewing temporary configuration
```

Other configuration attributes do not need to be continuously reflected as live calculated values. They can simply be validated and displayed as configuration state.

### Save Configuration

Only:

```text
[ SAVE CONFIGURATION ]
```

makes the temporary configuration permanent.

Recommended flow:

```text
Dashboard
    ↓
SAVE_CONFIG
    ↓
RX
    ↓
TX
    ↓
Validate
    ↓
Write to NVS
    ↓
Verify saved values
    ↓
Update active configuration
    ↓
CONFIG_SAVE_ACK
    ↓
Dashboard shows Synchronized
```

If Save fails, the previous saved configuration must remain intact.

### Discard / exit behavior

If the user leaves the Config page without saving:

```text
CONFIG_SESSION_END
        ↓
TX discards temporary configuration
        ↓
TX restores the last saved configuration
        ↓
TX returns to normal operation
        ↓
Deep sleep resumes
```

No temporary preview value should survive a configuration session unless it was explicitly saved.

### Configuration-session communication principle

Opening the Config page should not permanently change the TX's configuration and should not require physical access to the TX.

The complete interaction should be:

```text
Open Config
    ↓
Start configuration session
    ↓
TX remains awake
    ↓
Edit values
    ↓
Apply & Preview
    ↓
Live water % updates
    ↓
Edit again if required
    ↓
Apply & Preview again
    ↓
Satisfied?
    ├── YES → Save Configuration
    └── NO  → Continue editing
    ↓
Exit configuration session
    ↓
TX returns to normal low-power operation
```

The configuration session should be designed to feel immediate to the user while still preserving the TX's normal power-saving behavior outside the session.

The exact live-preview reporting interval should be finalized during implementation testing.

The exact initial values are examples and should be finalized during testing.

## Configurable parameters

Initially:

- Water top level.
- Water bottom level.
- Normal measurement interval.
- Motor-monitoring interval.
- Low-water threshold.
- Maximum motor runtime.

Hardware-level parameters should initially remain firmware constants.

---

# 9. Dashboard Configuration Synchronization

When the user changes a configuration:

```text
Dashboard
    ↓
Raspberry Pi backend
    ↓
MQTT
    ↓
RX ESP32-S3
    ↓
Pending configuration
    ↓
TX next wakes
    ↓
ESP-NOW
    ↓
TX ESP32-C3
    ↓
Validate
    ↓
NVS
```

The dashboard should show synchronization state.

Example:

```text
RX Config Version: 12
TX Config Version: 11

Status:
Waiting for TX next wake
```

After synchronization:

```text
RX Config Version: 12
TX Config Version: 12

Status:
Synchronized
```

---

# 10. Configuration Session Mode

The Raspberry Pi Config page introduces a temporary, interactive configuration mode for the TX.

This mode is different from the normal battery-saving configuration synchronization flow.

## 10.1 Normal configuration synchronization

Outside a configuration session:

```text
Dashboard changes saved configuration
        ↓
Raspberry Pi
        ↓
MQTT
        ↓
RX stores pending configuration
        ↓
TX wakes during its normal cycle
        ↓
ESP-NOW configuration exchange
        ↓
TX validates and saves
        ↓
Normal operation continues
```

This remains the preferred behavior for ordinary background configuration changes because it preserves battery life.

## 10.2 Interactive configuration session

When the user opens the Config page:

```text
Dashboard
    ↓
CONFIG_SESSION_START
    ↓
Raspberry Pi
    ↓
MQTT
    ↓
RX
    ↓
ESP-NOW
    ↓
TX
```

The TX acknowledges the command and enters:

```text
CONFIG_MODE
```

In this mode:

- TX does not enter normal deep sleep.
- TX periodically measures the tank.
- TX sends live readings to RX.
- RX forwards the readings to Raspberry Pi.
- The dashboard can display the current live water percentage.
- Temporary configuration can be applied without writing it to NVS.

## 10.3 Temporary configuration lifecycle

The TX should conceptually maintain:

```text
Saved Configuration
        │
        │ load at boot
        ▼
Active Configuration

Temporary Preview Configuration
        │
        └── created only during CONFIG_MODE
```

When `APPLY & PREVIEW` is received:

```text
Receive temporary configuration
        ↓
Validate
        ↓
Apply to RAM
        ↓
Do NOT write NVS
        ↓
Measure using temporary configuration
        ↓
Send live telemetry
```

When `SAVE_CONFIG` is received:

```text
Temporary configuration
        ↓
Validate again
        ↓
Write to NVS
        ↓
Verify
        ↓
Make it the saved/active configuration
        ↓
Send CONFIG_SAVE_ACK
```

If the session ends without saving:

```text
Temporary configuration
        ↓
Discard
        ↓
Restore saved configuration
        ↓
Return to normal operation
```

## 10.4 Why this approach is preferred

This gives the dashboard a live calibration-preview experience without permanently modifying the TX every time the user edits a field.

It also avoids the need to physically access the TX at the tank.

The battery-powered TX only sacrifices its normal deep-sleep behavior while the user is actively configuring the system.

## 10.5 Recommended configuration-session messages

Add the following logical commands to the ESP-NOW protocol:

```text
CONFIG_SESSION_START
CONFIG_SESSION_ACK

CONFIG_PREVIEW
CONFIG_PREVIEW_ACK

CONFIG_SAVE
CONFIG_SAVE_ACK

CONFIG_SESSION_END
CONFIG_SESSION_END_ACK
```

The exact packet structures should be finalized with the rest of the ESP-NOW protocol.

---

# 11. Automatic Motor Control

## Motor-Filling Power-State Rule

When the motor is ON and the tank is actively being filled, the TX must remain awake.

**The TX must not enter ESP32 deep sleep during motor filling.**

The TX may return to normal deep-sleep operation only after both conditions are true:

```text
water_percentage >= 100%
AND
motor_status == OFF
```

The motor-monitoring interval controls measurement/reporting frequency while the TX remains awake.

---


The new architecture adds automatic tank filling.

The basic control loop is:

```text
Tank level low
      ↓
RX turns motor ON
      ↓
TX monitors tank frequently
      ↓
Tank fills
      ↓
TX detects FULL
      ↓
TX sends FULL_DETECTED
      ↓
RX turns motor OFF
      ↓
RX confirms MOTOR OFF to TX
      ↓
TX returns to normal low-power mode
```

The RX controls the relay.

The TX measures the physical tank state.

This creates a closed-loop control system.

---

# 11. Why RX Controls the Relay

The relay is connected to the mains-powered side.

The RX is mains powered and is therefore the natural control node.

Architecture:

```text
TX:
"I measured 100%."

        ↓ ESP-NOW

RX:
"Tank full."

        ↓

Relay OFF

        ↓

Motor OFF
```

The TX should not directly control the mains relay.

This keeps responsibilities clean:

```text
TX = sensing
RX = gateway + real-time control
Pi = UI + history + configuration
```

---

# 12. Motor States

The system should have at least these logical states:

```text
NORMAL
MOTOR_FILLING
FULL_DETECTED
```

Additional internal states can be used as implementation details.

---

# 13. NORMAL State

When the motor is OFF:

```text
Motor OFF
↓
TX wakes every normal interval
↓
Measure
↓
Send telemetry
↓
Sleep
```

Example:

```text
Normal measurement interval = 5 minutes
```

So:

```text
10:00 measurement
10:05 measurement
10:10 measurement
10:15 measurement
```

The TX spends almost all its time asleep.

---

# 14. MOTOR_FILLING State

When the motor is ON and the tank is being filled, the TX must remain **awake continuously**.

The TX must **not enter ESP32 deep sleep** during this filling state.

The motor-monitoring interval controls how often the TX measures and reports the tank. It does not put the TX into deep sleep between measurements.

```text
Motor ON
↓
TX receives MOTOR_ON status
↓
TX enters MOTOR_FILLING / motor-monitoring mode
↓
Remain awake
↓
Measure
↓
Send level
↓
Wait for motor-monitoring interval
↓
Measure again
↓
Send level
↓
...
↓
Water >= 100%
↓
TX sends FULL_DETECTED
↓
RX turns motor OFF
↓
RX sends/confirm MOTOR_OFF
↓
TX verifies:
    water >= 100%
    AND motor == OFF
↓
Return to normal deep-sleep operation
```

Example:

```text
Normal measurement interval:
5 minutes

Motor monitoring interval:
15 seconds
```

The 15-second value is an initial design value and should be tested.

It can later be configured.

The motor-monitoring interval is therefore a **measurement/reporting interval while the TX remains awake**, not a deep-sleep interval.

---

# 15. TX Power Behavior During Motor Filling

During normal operation, the TX uses deep sleep to minimize battery consumption.

However, while the motor is ON and the tank is being filled, **reliable continuous monitoring takes priority over deep-sleep power saving**.

Therefore:

```text
Motor ON
↓
TX remains awake
↓
Measure at motor-monitoring interval
↓
Transmit
↓
Wait
↓
Measure again
↓
Transmit
↓
...
```

There must be **no ESP32 deep-sleep transition while the motor is ON and filling is in progress**.

The TX returns to deep sleep only after:

```text
water_percentage >= 100%
        AND
motor_status == OFF
```

This ensures that the TX remains available to detect the full condition and remains awake through the motor shutdown confirmation.

Battery efficiency is still preserved during normal motor-OFF operation, where the TX follows the normal deep-sleep cycle.

---

# 16. FULL Detection

When the water reaches the configured full level:

```text
TX measures:
distance <= WATER_TOP_LVL
```

and determines:

```text
water_percentage >= 100%
```

Then:

```text
TX → RX

FULL_DETECTED
```

RX immediately:

```text
Relay OFF
Motor OFF
```

Then:

```text
RX → TX

MOTOR_OFF
```

TX must confirm/observe that the motor is OFF.

The TX may return to normal deep-sleep operation **only after both conditions are satisfied**:

```text
water_percentage >= 100%
AND
motor_status == OFF
```

This means the TX does not immediately enter deep sleep merely because `FULL_DETECTED` was sent. The full-water condition and motor-OFF condition must both be satisfied first.

---

# 17. Motor Hysteresis

The system should avoid rapid motor ON/OFF cycling.

Recommended conceptual behavior:

```text
Water <= LOW_WATER_THRESHOLD
        ↓
Motor ON

Motor continues running
        ↓
Water >= FULL
        ↓
Motor OFF
```

Example:

```text
LOW_WATER_THRESHOLD = 5%

FULL = 100%
```

Therefore:

```text
Below 20% → eligible to start filling

20%-99% → continue filling

100% → stop filling
```

The exact motor-start policy should be finalized before implementation.

---

# 18. Motor Fail-Safe Timeout

The system must not rely only on the FULL packet.

RX should enforce a maximum motor runtime.

Example:

```text
Maximum motor runtime = 60 minutes
```

If:

```text
Motor ON
```

and:

```text
60 minutes elapsed
```

without a valid FULL event:

```text
RX → Relay OFF
```

This protects against:

- TX failure.
- ESP-NOW communication failure.
- Ultrasonic sensor failure.
- RX logic errors.
- Relay/control problems.
- Unexpected tank behavior.

The maximum runtime should be configurable from the dashboard.

---

# 19. Motor Failure Behavior

If the motor is ON and TX stops communicating:

```text
Motor ON
↓
TX telemetry stops
↓
RX detects missed reports
↓
RX should NOT keep pumping indefinitely
↓
Maximum runtime / safety logic
↓
Motor OFF
```

A future version can have a more immediate fail-safe based on missed motor-monitoring packets.

For the initial implementation, the maximum motor runtime is mandatory.

---

# 20. Motor State Synchronization

The RX is the authoritative source for motor state.

RX sends:

```text
MOTOR_ON
```

or:

```text
MOTOR_OFF
```

to TX.

TX includes motor state in telemetry where useful.

Example:

```json
{
    "water_percentage": 81,
    "motor_status": "ON"
}
```

This allows the Raspberry Pi to display the actual control state.

---

# 21. TX Measurement Lifecycle

## Normal operation

```text
                    DEEP SLEEP
                        │
                        │ timer expires
                        ▼
                      WAKE
                        │
                        ▼
                Initialize hardware
                        │
                        ▼
               Power ultrasonic ON
                        │
                        ▼
                Wait for stabilization
                        │
                        ▼
              Take water measurements
                        │
                        ▼
               Power sensor OFF
                        │
                        ▼
                Read battery voltage
                        │
                        ▼
              Calculate battery %
                        │
                        ▼
              Build telemetry packet
                        │
                        ▼
                  ESP-NOW → RX
                        │
                        ▼
                  Check motor state
                        │
                ┌───────┴────────┐
                │                │
             Motor OFF        Motor ON
                │                │
                ▼                ▼
          Normal deep sleep   MOTOR_FILLING
                                   │
                                   ▼
                              Remain awake
                                   │
                                   ▼
                         Measure at configured
                         motor-monitoring interval
                                   │
                                   ▼
                              Send telemetry
                                   │
                                   ▼
                              Check water
                                   │
                    ┌──────────────┴──────────────┐
                    │                             │
             water < 100%                  water >= 100%
                    │                             │
                    ▼                             ▼
              Measure again                 Send FULL_DETECTED
              while remaining awake               │
                                                  ▼
                                           RX turns motor OFF
                                                  │
                                                  ▼
                                           MOTOR_OFF → TX
                                                  │
                                                  ▼
                                      Confirm/observe motor OFF
                                                  │
                                                  ▼
                                      water >= 100% AND
                                      motor == OFF
                                                  │
                                                  ▼
                                         Normal deep sleep
```

## Important power-state rule

While the motor is ON and the tank is being filled:

```text
TX remains awake.
TX does NOT enter ESP32 deep sleep.
```

The configured `MOTOR_MONITORING_INTERVAL` controls the measurement/reporting frequency while the TX remains awake.

The TX may return to normal deep sleep only when:

```text
water_percentage >= 100%
AND
motor_status == OFF
```

This rule takes priority over the normal battery-saving sleep behavior during active tank filling.

# 22. Existing Sensor Code to Preserve

The old system already has real-world-tested battery and ultrasonic sensor behavior.

This is important hardware-specific knowledge and should be preserved unless there is a demonstrated reason to change it.

The new architecture should build around this validated sensing layer.

---

# 23. Existing Battery Measurement Logic

Existing function:

```cpp
int Battery_Percentage() {

  static unsigned long lastBatteryCheck = 0;

  static int cachedBatteryPercentage = 0;

  static bool firstRun = true;

  const unsigned long BATTERY_UPDATE_INTERVAL = 120000; // 2 minutes

  if (!firstRun && (millis() - lastBatteryCheck < BATTERY_UPDATE_INTERVAL)) {

    return cachedBatteryPercentage;

  }

  const int NUM_READINGS = 100;

  long adc_sum = 0;

  analogRead(ANALOG_IN_PIN);

  for (int i = 0; i < NUM_READINGS; i++) {

    adc_sum += analogRead(ANALOG_IN_PIN);

  }

  int adc_value = adc_sum / NUM_READINGS;

  float voltage_adc = ((float)adc_value * REF_VOLTAGE) / ADC_RESOLUTION;

  float voltage_battery = voltage_adc * (R1 + R2) / R2;

  voltage_battery = voltage_battery * 1.237;

  voltage_battery = constrain(voltage_battery, 2.9, 4.0);

  float battery_percentage =
      ((voltage_battery - 2.9) / (4.0 - 2.9)) * 100.0;

  cachedBatteryPercentage =
      (int)constrain(battery_percentage, 0, 100);

  lastBatteryCheck = millis();

  firstRun = false;

  return cachedBatteryPercentage;
}
```

---

# 24. Battery Hardware-Specific Details

- Battery ADC has already been tested.
- Voltage divider uses R1 = R2 = 100k.
- Calibration factor currently used: `1.237`.
- ADC measurement uses 100 samples.
- A dummy ADC read is performed before sampling.
- Battery voltage is clamped to 2.9 V - 4.0 V.
- Percentage is mapped from 2.9 V - 4.0 V.
- This calibration was based on actual hardware.

---

# 25. Deep-Sleep Adjustment to Battery Code

The current caching mechanism is unnecessary for the new TX architecture because the device wakes, measures, transmits, and sleeps.

The simplified function can be:

```cpp
int Battery_Percentage()
{
    const int NUM_READINGS = 100;

    long adc_sum = 0;

    analogRead(ANALOG_IN_PIN);

    for (int i = 0; i < NUM_READINGS; i++) {
        adc_sum += analogRead(ANALOG_IN_PIN);
    }

    int adc_value = adc_sum / NUM_READINGS;

    float voltage_adc =
        ((float)adc_value * REF_VOLTAGE) / ADC_RESOLUTION;

    float voltage_battery =
        voltage_adc * (R1 + R2) / R2;

    voltage_battery *= 1.237;

    voltage_battery =
        constrain(voltage_battery, 2.9, 4.0);

    float percentage =
        ((voltage_battery - 2.9) /
        (4.0 - 2.9)) * 100.0;

    return (int)constrain(percentage, 0, 100);
}
```

The validated measurement/calibration behavior is retained.

---

# 26. Existing Water Measurement Logic

Existing function:

```cpp
int Water_Percentage() {
  static int lastValidPercentage = 100;

  digitalWrite(SENSOR_PWR_PIN, HIGH);
  delay(50);

  int avg_level = 0;
  for (int i = 0; i < 10; i++) {
    avg_level += sensor.getCM();
    delay(5);
  }

  digitalWrite(SENSOR_PWR_PIN, LOW);

  int level = avg_level / 10;
  level = constrain(level, WATER_TOP_LVL, WATER_BOTTOM_LVL);

  int percentage = (WATER_BOTTOM_LVL - level) * 100 /
                   (WATER_BOTTOM_LVL - WATER_TOP_LVL);

  percentage = constrain(percentage, 1, 100);

  if (percentage < 6) {
    int confirmSum = 0;

    digitalWrite(SENSOR_PWR_PIN, HIGH);
    delay(30);

    for (int i = 0; i < 10; i++) {
      confirmSum += sensor.getCM();
      delay(100);
    }

    digitalWrite(SENSOR_PWR_PIN, LOW);

    int confirmLevel = confirmSum / 10;
    confirmLevel = constrain(
        confirmLevel,
        WATER_TOP_LVL,
        WATER_BOTTOM_LVL
    );

    int confirmPercentage =
        (WATER_BOTTOM_LVL - confirmLevel) * 100 /
        (WATER_BOTTOM_LVL - WATER_TOP_LVL);

    if (confirmPercentage < 6) {
      lastValidPercentage =
          max(confirmPercentage, 1);

      return lastValidPercentage;
    } else {
      return lastValidPercentage;
    }
  }

  lastValidPercentage = percentage;

  return percentage;
}
```

---

# 27. Why the Existing Water Logic Is Valuable

The code has several useful characteristics.

## Sensor power control

```cpp
digitalWrite(SENSOR_PWR_PIN, HIGH);
```

The ultrasonic sensor is only powered when required.

After measurement:

```cpp
digitalWrite(SENSOR_PWR_PIN, LOW);
```

This is particularly appropriate for the battery-powered TX.

## Multiple measurements

10 readings are averaged.

## Low-water glitch detection

If the result is below 6%, the system performs a slower confirmation measurement.

## Last-valid-value behavior

A suspicious low reading can be discarded rather than immediately reported.

This should be retained in V2.

---

# 28. Recommended Water Sensor API

Instead of returning only an integer, V2 should eventually separate measurement from interpretation.

Recommended:

```cpp
struct WaterReading {
    int distanceCm;
    int percentage;
    bool valid;
};
```

Then:

```cpp
WaterReading ReadWaterSensor();
```

This allows the system to retain both:

```text
distance_cm
water_percentage
```

and sensor validity.

Example:

```text
distance_cm = 63
percentage = 81
valid = true
```

---

# 29. Preserve Raw Distance

The TX should transmit both:

```text
water_distance_cm
water_percentage
```

Reason:

If only percentage is transmitted, the raw physical measurement is lost.

With both values, the dashboard/database can later recalculate or analyze the tank level.

Example:

```text
Distance = 70 cm
Water percentage = 57%
```

---

# 30. Sensor Fault Representation

The system should eventually distinguish:

### Normal

```text
distance = 63 cm
status = OK
```

### Valid extreme level

```text
distance = 10 cm
status = OK
percentage = 100
```

### Sensor failure

```text
distance = invalid
status = SENSOR_ERROR
```

The current `constrain()` behavior can remain during the initial migration, but explicit sensor validity should be added during V2 implementation.

---

# 31. Water Level Calibration Reference

`WATER_TOP_LVL` and `WATER_BOTTOM_LVL` are **ultrasonic sensor distances**, measured from the ultrasonic sensor downward to the water surface reference levels.

They do **not** represent the physical height of the tank from its bottom.

The expected physical arrangement is:

```text
                 ULTRASONIC SENSOR
                        │
                        │
                        │
        ┌───────────────┼───────────────┐
        │               │               │
        │               │               │
        │               │               │
        │               │               │
        │               │               │
        │               │               │
        │~~~~~~~~~~~~~~~│~~~~~~~~~~~~~~~│
        │       current water surface   │
        │                               │
        │               │               │
        │               │               │
        │               │               │
        └───────────────────────────────┘

        TOP_LVL    = distance from sensor
                     to FULL reference level

        BOTTOM_LVL = distance from sensor
                     to EMPTY reference level
```

## WATER_TOP_LVL

`WATER_TOP_LVL` is the distance from the ultrasonic sensor to the **maximum/full-water reference level**.

Example:

```text
WATER_TOP_LVL = 10 cm
```

means:

```text
Measured sensor distance = 10 cm
→ Tank is considered 100% full
```

## WATER_BOTTOM_LVL

`WATER_BOTTOM_LVL` is the distance from the ultrasonic sensor to the **minimum/empty-water reference level**.

Example:

```text
WATER_BOTTOM_LVL = 150 cm
```

means:

```text
Measured sensor distance = 150 cm
→ Tank is considered 0% full
```

## Water Percentage Calculation

For:

```text
WATER_TOP_LVL    = 10 cm
WATER_BOTTOM_LVL = 150 cm
```

the water percentage is calculated from the measured ultrasonic distance:

```cpp
waterPercentage =
    (WATER_BOTTOM_LVL - measuredDistance) * 100.0 /
    (WATER_BOTTOM_LVL - WATER_TOP_LVL);
```

Expected mapping:

```text
Measured Distance     Water Percentage

10 cm                  100%
50 cm                  ~71%
80 cm                   50%
100 cm                 ~36%
150 cm                   0%
```

The relationship is:

```text
Smaller measured distance → More water
Larger measured distance  → Less water

WATER_TOP_LVL             → 100%
WATER_BOTTOM_LVL          → 0%
```

The measured distance must be constrained to the configured calibration range before calculating the percentage.

These calibration values are runtime configuration parameters and must be used consistently by the TX sensor calculation, temporary configuration preview, and saved configuration.

---

# 33. Tank Configuration

`WATER_TOP_LVL` and `WATER_BOTTOM_LVL` must follow the ultrasonic sensor-distance definition in Section 31.

The following should become runtime configuration instead of compile-time-only constants:

```text
WATER_TOP_LVL
WATER_BOTTOM_LVL
NORMAL_MEASUREMENT_INTERVAL
MOTOR_MONITORING_INTERVAL
LOW_WATER_THRESHOLD
MAX_MOTOR_RUNTIME
```

Example:

```text
WATER_TOP_LVL = 10 cm
WATER_BOTTOM_LVL = 150 cm
NORMAL_MEASUREMENT_INTERVAL = 300 seconds
MOTOR_MONITORING_INTERVAL = 15 seconds
LOW_WATER_THRESHOLD = 20%
MAX_MOTOR_RUNTIME = 3600 seconds
```

These are initial examples and should be finalized through real testing.

---

# 33. Configuration Storage

TX configuration should be stored in ESP32-C3 NVS.

Recommended:

```cpp
struct TankConfig {

    float waterTopLevel;
    float waterBottomLevel;

    uint32_t normalSleepInterval;
    uint32_t motorMonitoringInterval;

    uint8_t lowWaterThreshold;

    uint32_t maxMotorRuntime;

    uint32_t configVersion;
};
```

Example:

```text
waterTopLevel = 10
waterBottomLevel = 150
normalSleepInterval = 300
motorMonitoringInterval = 15
lowWaterThreshold = 20
maxMotorRuntime = 3600
configVersion = 7
```

---

# 34. Configuration Versioning

A configuration version is strongly recommended.

Example:

```text
RX config version = 17
TX config version = 16
```

When TX wakes:

```text
TX → SENSOR_DATA
config_version = 16

RX sees:
my_config_version = 17
```

RX then sends:

```text
CONFIG_RESPONSE
config_version = 17
```

TX stores the configuration in NVS.

The next TX packet reports:

```text
config_version = 17
```

This avoids blindly sending configuration every cycle.

---

# 35. Configuration Timing

The TX remains autonomous.

Example:

```text
TX interval = 10 minutes
```

If configuration is changed at 10:02:

```text
10:02 configuration changed
10:10 TX wakes
10:10 configuration transferred
```

The configuration becomes active on the next TX communication cycle.

This preserves battery life.

---

# 36. ESP-NOW Protocol

The system should use a small binary packet protocol rather than JSON over ESP-NOW.

Suggested message types:

```cpp
enum MessageType {

    SENSOR_DATA,

    ACK,

    CONFIG_REQUEST,

    CONFIG_RESPONSE,

    CONFIG_ACK,

    MOTOR_ON,

    MOTOR_OFF,

    FULL_DETECTED
};
```

Future message types can be added later:

```text
PING
REQUEST_STATUS
CALIBRATE_SENSOR
REBOOT
UPDATE_INTERVAL
```

---

# 37. Sensor Data Packet

Suggested structure:

```cpp
struct SensorPacket {

    uint8_t messageType;

    uint32_t sequence;

    uint32_t configVersion;

    uint16_t waterDistanceCm;

    uint8_t waterPercentage;

    uint16_t batteryVoltageMv;

    uint8_t batteryPercentage;

    uint8_t sensorStatus;

    uint8_t motorStatus;
};
```

Potential fields:

```text
messageType
sequence
configVersion
waterDistance
waterPercentage
batteryVoltage
batteryPercentage
sensorStatus
motorStatus
```

These structures are starting designs and should be finalized before implementation.

---

# 38. FULL_DETECTED Event

When the tank reaches full level:

```text
TX → RX

messageType = FULL_DETECTED
sequence = current sequence
waterDistance = current distance
waterPercentage = 100
motorStatus = ON
```

RX must then:

```text
1. Validate event.
2. Turn relay OFF.
3. Update motor state.
4. Record motor stop reason.
5. Send MOTOR_OFF to TX.
6. Publish event to Raspberry Pi.
```

---

# 39. ACK and Retry Logic

The system should distinguish between ordinary telemetry and important control/configuration commands.

An application-level ACK is useful when the sender needs confirmation that the peer not only received a packet but also processed the command.

## 38.1 Normal telemetry

For ordinary periodic water/battery telemetry, the TX does not necessarily need a custom application-level ACK for every packet.

ESP-NOW send/delivery status can be used as the first level of communication feedback.

```text
TX sends SENSOR_DATA
        ↓
ESP-NOW send result
        ↓
SUCCESS → continue
FAILURE → retry according to policy
```

A lost periodic telemetry packet is normally tolerable because another measurement will arrive during the next cycle.

This avoids unnecessarily extending the battery-powered TX awake time for every routine reading.

## 38.2 Important commands

Important state-changing commands should use an application-level ACK.

Examples:

```text
RX → TX
MOTOR_ON

TX → RX
COMMAND_ACK
```

```text
RX → TX
MOTOR_OFF

TX → RX
COMMAND_ACK
```

Configuration commands should similarly be acknowledged:

```text
RX → TX
CONFIG_PREVIEW

TX → RX
CONFIG_PREVIEW_ACK
```

and:

```text
RX → TX
CONFIG_SAVE

TX → RX
CONFIG_SAVE_ACK
```

This provides confirmation that the command was received and processed.

## 38.3 Retry behavior

For an important command:

```text
RX/TX sends command
        ↓
wait for command ACK
        ↓
ACK received?
   /          \
 YES           NO
  │             │
continue      retry
                │
             retry limit
                │
             failure
```

Sequence numbers should be used so that retries do not accidentally perform the same operation more than once.

Commands should preferably be idempotent where practical. For example, receiving `MOTOR_OFF` twice should still result in:

```text
Motor OFF
```

rather than causing an unexpected state transition.

## 38.4 ACK timeout and retries

Initial starting values can remain:

```text
MAX_RETRIES = 3
ACK_TIMEOUT = 100 ms
```

but these values should be measured and optimized during real hardware testing.

For the battery-powered TX, the final timeout should be kept as short as reliably possible.

## 38.5 Power impact

Waiting for an ACK keeps the TX awake slightly longer than a one-way transmission.

For successful communication, the additional active time should normally be small compared with the complete sensor measurement cycle.

The exact duration depends on:

- ESP-NOW radio timing.
- Packet size.
- ACK timeout.
- Processing time.
- Wi-Fi/ESP-NOW channel conditions.
- Retries.

Therefore the implementation should measure actual TX awake time rather than assuming a fixed value.

The design goal is:

```text
Normal telemetry:
minimal awake time

Important commands:
slightly longer awake time for confirmation

Configuration session:
TX intentionally remains awake while the user is actively configuring
```

This is the preferred power/reliability tradeoff for the system.

---

# 40. Motor Control Communication

When RX starts the motor:

```text
RX → TX

MOTOR_ON
```

TX then switches into motor-monitoring mode and **remains awake continuously** while the motor is ON and the tank is being filled.

When TX detects full:

```text
TX → RX

FULL_DETECTED
```

RX:

```text
Relay OFF
Motor OFF
```

Then:

```text
RX → TX

MOTOR_OFF
```

TX confirms/observes the motor-OFF state.

TX returns to normal deep-sleep operation only when:

```text
water_percentage >= 100%
AND
motor_status == OFF
```

---

# 41. Motor Start Decision

The initial system should use the configured low-water threshold.

Example:

```text
Water <= 20%
```

can cause:

```text
RX → Relay ON
RX → TX: MOTOR_ON
```

However, the exact motor-start trigger should be finalized after deciding whether the motor should start automatically based on every low-water reading or through another control policy.

The motor-stop condition is more important initially:

```text
Water reaches FULL
→ Motor OFF
```

---

# 42. Motor State Authority

The RX is authoritative for:

```text
motor_status
```

The TX reports the physical water condition.

The Pi displays and records the RX-controlled motor state.

This avoids ambiguous situations where different components believe the motor is in different states.

---

# 43. RX Telemetry Flow

When RX receives:

```text
SENSOR_DATA
```

it should:

1. Validate packet.
2. Check sequence.
3. Record receive time.
4. Provide ESP-NOW delivery/application acknowledgment according to packet type.
5. Update latest TX state.
6. Determine whether motor logic needs to react.
7. Publish telemetry to MQTT.
8. Raspberry Pi stores it.
9. Dashboard updates.

Flow:

```text
TX
 │
 │ SENSOR_DATA
 ▼
RX
 │
 ├── ACK → TX
 │
 ├── Motor logic
 │
 └── MQTT → Raspberry Pi
              │
              ├── Database
              │
              └── Dashboard
```

---

# 44. RX Configuration Flow

### Normal saved configuration update

Dashboard sends:

```text
CONFIG_SET
```

to Raspberry Pi backend.

Backend publishes:

```text
tank/tx01/config/set
```

RX receives it.

RX:

```text
validate config
↓
store pending config
↓
wait for TX
```

When TX wakes:

```text
TX → SENSOR_DATA
```

RX sees that the TX has an old configuration version.

RX responds with:

```text
ACK
+
CONFIG_RESPONSE
```

TX validates and saves it.

### Interactive configuration-session flow

When the Config page is actively open:

```text
Dashboard
    ↓
CONFIG_SESSION_START
    ↓
Backend / MQTT
    ↓
RX
    ↓
ESP-NOW
    ↓
TX
```

TX enters configuration mode and remains awake.

When the user changes a value and presses:

```text
APPLY & PREVIEW
```

the backend sends a temporary configuration command.

```text
Dashboard
    ↓
CONFIG_PREVIEW
    ↓
RX
    ↓
TX
    ↓
Validate
    ↓
Apply to RAM only
    ↓
CONFIG_PREVIEW_ACK
```

The TX then reports live water data using the temporary configuration.

The dashboard can therefore show the immediate effect of changing `WATER_TOP_LVL` or `WATER_BOTTOM_LVL`.

Only when the user presses:

```text
SAVE CONFIGURATION
```

does the configuration get written permanently to TX NVS.

When the user leaves the page without saving:

```text
CONFIG_SESSION_END
    ↓
TX discards temporary configuration
    ↓
TX restores saved configuration
    ↓
TX resumes normal low-power operation
```

---

# 45. RX Motor State Machine

Recommended:

```text
                     ┌───────────────┐
                     │ MOTOR OFF     │
                     └───────┬───────┘
                             │
                      low water condition
                             │
                             ▼
                     ┌───────────────┐
                     │ MOTOR ON      │
                     │ FILLING       │
                     └───────┬───────┘
                             │
                    ┌────────┴────────┐
                    │                 │
               FULL detected      timeout
                    │                 │
                    └────────┬────────┘
                             ▼
                     ┌───────────────┐
                     │ MOTOR OFF     │
                     └───────────────┘
```

Any safety timeout should always result in the relay being turned OFF.

---

# 46. RX Motor Control Rules

When motor is OFF:

```text
If water <= low-water threshold
    AND system is allowed to fill
        → Motor ON
```

When motor is ON:

```text
If FULL_DETECTED
    → Motor OFF

OR

If maximum runtime exceeded
    → Motor OFF
```

After motor OFF:

```text
Send MOTOR_OFF to TX
↓
TX confirms/observes MOTOR_OFF
↓
Check water_percentage >= 100%
↓
If both full and motor OFF:
    Return TX to normal deep-sleep cycle
```

The motor-monitoring state must not transition directly to deep sleep on `MOTOR_OFF` alone if the full-water condition has not yet been satisfied.

---

# 47. TX Does Not Depend on RX Being Online

If:

```text
Raspberry Pi OFF
```

or:

```text
Wi-Fi OFF
```

or:

```text
RX OFF
```

the TX should still:

```text
wake
measure
attempt ESP-NOW
fail if necessary
sleep
```

When communication becomes available again, the next cycle resumes normal operation.

---

# 48. Important Motor Safety Principle

The motor must have a safe OFF behavior.

Software should not assume:

```text
FULL_DETECTED always arrives.
```

Instead:

```text
FULL_DETECTED
        OR
maximum runtime timeout
        OR
other safety fault
        ↓
MOTOR OFF
```

The relay should default to OFF during boot or undefined states where practical.

Electrical safety and mains isolation must be handled appropriately in the physical relay installation.

---

# 49. Raspberry Pi MQTT Architecture

Recommended:

```text
                         Raspberry Pi 5
┌─────────────────────────────────────────────────┐
│                                                 │
│  Web Browser                                    │
│       │                                         │
│       ▼                                         │
│  ┌──────────────┐                               │
│  │ Dashboard    │                               │
│  └──────┬───────┘                               │
│         │ HTTP/API                              │
│         ▼                                       │
│  ┌──────────────┐                               │
│  │ Backend/API  │                               │
│  └──────┬───────┘                               │
│         │                                       │
│    ┌────┴───────────────┐                       │
│    │                    │                       │
│    ▼                    ▼                       │
│ MQTT Broker          SQLite DB                  │
│    │                                            │
└────┼────────────────────────────────────────────┘
     │
     │ Wi-Fi / MQTT
     ▼
 ESP32-S3 RX
```

---

# 50. MQTT Topic Structure

Recommended initial topics:

```text
tank/tx01/telemetry
tank/tx01/status
tank/tx01/motor
tank/tx01/config/set
tank/tx01/config/state
tank/tx01/command
tank/tx01/event
```

Telemetry example:

```json
{
    "sequence": 1827,
    "distance_cm": 63,
    "water_percentage": 81,
    "battery_voltage": 3.87,
    "battery_percentage": 88,
    "motor_status": "OFF",
    "config_version": 12
}
```

Motor event example:

```json
{
    "event": "FULL_DETECTED",
    "water_percentage": 100,
    "motor_status": "ON"
}
```

---

# 49.1 MQTT Broker Deployment (already done at Pi)

Platform:
Raspberry Pi 5
ARM64 / aarch64

MQTT port:
1883

Protocol:
MQTT over TCP

Connection Details:

    MQTT Broker Host:
    192.168.29.211

    MQTT Broker Port:
    1883

# 51. Raspberry Pi Database

Store at least:

```text
telemetry
---------
id
timestamp
device_id
sequence
distance_cm
water_percentage
battery_voltage
battery_percentage
config_version
motor_status
sensor_status
```

Configuration table:

```text
config
------
device_id
config_version
water_top_level
water_bottom_level
normal_interval
motor_monitor_interval
low_water_threshold
max_motor_runtime
updated_at
```

Motor history:

```text
motor_events
------------
id
device_id
timestamp
event
water_percentage
reason
runtime_seconds
```

Device status:

```text
device_status
-------------
device_id
last_seen
last_sequence
tx_config_version
rx_config_version
communication_status
motor_status
```

---

# 52. Historical Graph Data

The Graph page can use:

```text
timestamp
water_percentage
distance_cm
battery_percentage
battery_voltage
motor_status
```

This enables:

- Water level history.
- Tank filling curves.
- Tank draining curves.
- Battery behavior.
- Motor runtime history.
- Correlation between motor operation and tank level.

---

# 53. TX Online/Offline Detection

The Raspberry Pi should understand the TX's current operating mode.

If:

```text
Motor OFF
```

and normal interval is:

```text
5 minutes
```

expected reports are roughly:

```text
10:00
10:05
10:10
10:15
```

If:

```text
Motor ON
```

and motor interval is:

```text
15 seconds
```

expected reports are much more frequent.

Therefore the offline detector should use:

```text
current TX mode
+
configured interval
+
grace period
```

rather than one fixed timeout.

---

# 54. Example Online Detection

Normal:

```text
Expected interval = 5 min
Grace = 2 min
```

Motor filling:

```text
Expected interval = 15 sec
Grace = several expected cycles
```

This prevents the dashboard from incorrectly declaring the TX offline simply because it is sleeping normally.

---

# 55. Battery Monitoring

The TX should ideally send both:

```text
battery_voltage
battery_percentage
```

Example:

```text
battery_voltage = 3.87 V
battery_percentage = 88%
```

Battery percentage is useful for UI.

Raw voltage is useful for diagnostics and future battery/solar analysis.

---

# 56. Solar/Battery Optimization

The TX should minimize active time.

Normal:

```text
Deep Sleep
    ↓
Wake
    ↓
Sensor ON
    ↓
Measurement
    ↓
Sensor OFF
    ↓
Battery measurement
    ↓
ESP-NOW
    ↓
Deep Sleep
```

Motor filling:

```text
Short Deep Sleep
    ↓
Wake
    ↓
Sensor ON
    ↓
Measurement
    ↓
Sensor OFF
    ↓
ESP-NOW
    ↓
Short Deep Sleep
    ↓
Repeat
```

Do not leave the ultrasonic sensor powered continuously.

The existing `SENSOR_PWR_PIN` approach should be retained.

---

# 57. Suggested TX Firmware Structure

Recommended directory:

```text
water-monitor/
│
├── tx/
│   ├── tx.ino
│   │
│   ├── config.h
│   ├── config.cpp
│   │
│   ├── battery.h
│   ├── battery.cpp
│   │
│   ├── water_sensor.h
│   ├── water_sensor.cpp
│   │
│   ├── espnow.h
│   ├── espnow.cpp
│   │
│   ├── protocol.h
│   ├── protocol.cpp
│   │
│   ├── motor_state.h
│   ├── motor_state.cpp
│   │
│   ├── sleep_manager.h
│   └── sleep_manager.cpp
│
├── rx/
│   ├── rx.ino
│   ├── espnow_gateway.h
│   ├── espnow_gateway.cpp
│   ├── wifi_manager.h
│   ├── wifi_manager.cpp
│   ├── mqtt_client.h
│   ├── mqtt_client.cpp
│   ├── config_manager.h
│   ├── config_manager.cpp
│   ├── motor_controller.h
│   └── motor_controller.cpp
│
└── raspberrypi/
    ├── dashboard/
    │   ├── landing/
    │   ├── graph/
    │   └── config/
    │
    ├── backend/
    └── docker-compose.yml
```

---

# 58. Recommended TX Modules

## battery.cpp

Responsible for:

- ADC sampling.
- Battery voltage calculation.
- Existing calibration factor.
- Battery percentage.

## water_sensor.cpp

Responsible for:

- Sensor power.
- Ultrasonic measurement.
- Averaging.
- Low-water confirmation.
- Sensor fault detection.
- Water percentage.

## protocol.cpp

Responsible for:

- Packet structures.
- Message types.
- Serialization.
- Sequence numbers.

## espnow.cpp

Responsible for:

- ESP-NOW initialization.
- Peer setup.
- Sending.
- ESP-NOW delivery status.
- Application-level ACK handling for important commands.
- Retry handling.
- Configuration-session communication.
- Receiving configuration.
- Receiving motor state.

## config.cpp

Responsible for:

- NVS.
- Loading configuration.
- Saving configuration.
- Configuration version.

## motor_state.cpp

Responsible for:

- Tracking current motor state.
- Switching between normal and motor-monitoring modes.
- Handling MOTOR_ON/MOTOR_OFF.
- Detecting FULL condition.

## sleep_manager.cpp

Responsible for:

- Normal wake interval.
- Motor-monitoring wake interval.
- Deep-sleep setup.
- Sleep entry.

---

# 59. Recommended RX Modules

## espnow_gateway.cpp

Responsible for:

- ESP-NOW initialization.
- RX packet handling.
- ESP-NOW delivery/application ACKs.
- TX configuration exchange.
- Configuration-session handling.
- Motor state communication.

## wifi_manager.cpp

Responsible for:

- Wi-Fi connection.
- Reconnection.
- Channel handling.

## mqtt_client.cpp

Responsible for:

- MQTT connection.
- Telemetry publishing.
- Configuration subscription.
- Motor status publishing.

## config_manager.cpp

Responsible for:

- Pending configuration.
- Configuration version.
- Validation.
- Mapping MQTT config to ESP-NOW config.

## motor_controller.cpp

Responsible for:

- Relay GPIO.
- Motor ON/OFF state.
- Motor start time.
- Maximum runtime timeout.
- FULL_DETECTED handling.
- Safety shutdown.

---

# 60. ESP-NOW Packet Design

Initial protocol:

```cpp
enum MessageType {
    SENSOR_DATA,
    ACK,
    CONFIG_REQUEST,
    CONFIG_RESPONSE,
    CONFIG_ACK,
    CONFIG_SESSION_START,
    CONFIG_SESSION_ACK,
    CONFIG_PREVIEW,
    CONFIG_PREVIEW_ACK,
    CONFIG_SAVE,
    CONFIG_SAVE_ACK,
    CONFIG_SESSION_END,
    CONFIG_SESSION_END_ACK,
    MOTOR_ON,
    MOTOR_OFF,
    FULL_DETECTED
};
```

Sensor packet:

```cpp
struct SensorPacket {

    uint8_t messageType;

    uint32_t sequence;

    uint32_t configVersion;

    uint16_t waterDistanceCm;

    uint8_t waterPercentage;

    uint16_t batteryVoltageMv;

    uint8_t batteryPercentage;

    uint8_t sensorStatus;

    uint8_t motorStatus;
};
```

ACK:

```cpp
struct AckPacket {

    uint8_t messageType;

    uint32_t sequence;

    uint32_t configVersion;
};
```

Configuration:

```cpp
struct ConfigPacket {

    uint8_t messageType;

    uint32_t configVersion;

    uint16_t waterTopLevelCm;

    uint16_t waterBottomLevelCm;

    uint32_t normalSleepIntervalSeconds;

    uint32_t motorMonitoringIntervalSeconds;

    uint8_t lowWaterThreshold;

    uint32_t maxMotorRuntimeSeconds;
};
```

These structures are starting designs and should be finalized before implementation.

---

# 61. Configuration Validation

TX should never blindly accept configuration.

Example validation:

```text
waterTopLevel > 0
waterBottomLevel > waterTopLevel
normalSleepInterval >= minimum allowed interval
motorMonitoringInterval >= minimum allowed interval
maxMotorRuntime > motorMonitoringInterval
lowWaterThreshold > 0
lowWaterThreshold < 100
```

Reject obviously invalid configurations.

Example:

```text
Top = 150 cm
Bottom = 10 cm
```

should be rejected.

---

# 62. TX Sequence Number

Every TX wake/measurement cycle gets a sequence number.

Example:

```text
TX packet:
sequence = 1827
```

RX ACK:

```text
ACK:
sequence = 1827
```

This allows:

- ACK matching.
- Duplicate detection.
- Telemetry ordering.
- Communication diagnostics.

---

# 63. Timekeeping

The TX does not initially need an accurate real-time clock.

The Raspberry Pi can assign authoritative timestamps when telemetry arrives.

Example:

```text
TX:
sequence = 1827

RX receives at:
2026-09-19 10:45:03
```

Database stores:

```text
timestamp = 2026-09-19 10:45:03
```

This avoids unnecessary time synchronization work on the battery-powered TX.

---

# 64. Failure Scenarios

## Scenario A: RX unavailable

```text
TX wakes
↓
Measure
↓
ESP-NOW send
↓
No ACK
↓
Retry
↓
No ACK
↓
Sleep
```

Next cycle tries again.

---

## Scenario B: Raspberry Pi unavailable

```text
TX → RX
       ↓
      ACK
       ↓
RX receives telemetry
       ↓
MQTT unavailable
```

RX should ideally retain the latest telemetry in RAM and continue ACKing TX.

The Pi can reconnect later.

Future versions can add buffering.

---

## Scenario C: Wi-Fi temporarily unavailable

RX continues trying to reconnect to Wi-Fi.

ESP-NOW should remain available if possible.

When Wi-Fi returns:

```text
RX → MQTT → Pi
```

---

## Scenario D: TX sensor glitch

The existing low-water confirmation logic should prevent a single suspicious low reading from immediately becoming the official reading.

---

## Scenario E: Configuration transfer fails

TX continues using the previous valid configuration.

Never erase a working configuration before the new configuration has been validated and safely stored.

---

## Scenario F: TX stops communicating while motor is ON

```text
Motor ON
↓
TX telemetry stops
↓
RX detects missing motor-monitoring packets
↓
Motor continues only until safety timeout
↓
RX turns motor OFF
```

A future implementation can make this even more conservative by stopping the motor after a configured number of missed motor-monitoring reports.

---

## Scenario G: Sensor falsely reports FULL

A false FULL event could stop the motor early.

This is safer than allowing overflow.

The system should favor a conservative motor-stop behavior when uncertain.

---

# 65. Configuration Update Safety

Configuration handling has two paths: temporary preview and permanent save.

## Temporary preview

```text
Receive CONFIG_PREVIEW
      ↓
Validate
      ↓
Apply to RAM
      ↓
Do NOT write NVS
      ↓
Send CONFIG_PREVIEW_ACK
```

If invalid:

```text
Reject
↓
Continue previous active configuration
```

## Permanent save

```text
Receive CONFIG_SAVE
      ↓
Validate
      ↓
Write new config to NVS
      ↓
Verify saved values
      ↓
Update saved/active configuration
      ↓
Send CONFIG_SAVE_ACK
```

If invalid or if the save cannot be verified:

```text
Reject
↓
Keep previous valid saved configuration
```

This prevents a malformed dashboard command or failed write from breaking the TX.

The previous valid NVS configuration must remain recoverable until the new configuration has been validated and safely stored.

---

# 66. Motor Safety Update

Motor control should follow:

```text
Motor ON
   │
   ├── FULL_DETECTED → OFF
   │
   ├── MAX_RUNTIME → OFF
   │
   └── communication/safety fault → OFF
```

The system should fail toward:

```text
Motor OFF
```

rather than:

```text
Motor ON indefinitely
```

---

# 67. Initial Constants

Suggested starting values:

```cpp
constexpr uint32_t DEFAULT_NORMAL_INTERVAL_SEC = 300;

constexpr uint32_t DEFAULT_MOTOR_MONITOR_INTERVAL_SEC = 15;

constexpr int WATER_SAMPLE_COUNT = 10;

constexpr int BATTERY_SAMPLE_COUNT = 100;

constexpr int SENSOR_STARTUP_DELAY_MS = 50;

constexpr int LOW_WATER_CONFIRM_THRESHOLD_PERCENT = 6;

constexpr int LOW_WATER_CONFIRM_SAMPLE_COUNT = 10;

constexpr int LOW_WATER_CONFIRM_DELAY_MS = 100;

constexpr int ESP_NOW_MAX_RETRIES = 3;

constexpr int ESP_NOW_ACK_TIMEOUT_MS = 100;

constexpr uint32_t DEFAULT_LOW_WATER_THRESHOLD_PERCENT = 20;

constexpr uint32_t DEFAULT_MAX_MOTOR_RUNTIME_SEC = 3600;
```

These are starting values, not final specifications.

The existing tested sensor behavior should take precedence over arbitrary optimization.

---

# 68. Existing Hardware Calibration Must Be Preserved

Important known-good information:

## Battery

```text
ADC divider:
R1 = 100k
R2 = 100k

Calibration factor:
1.237

Effective battery range used:
2.9 V - 4.0 V

ADC averaging:
100 readings
```

## Ultrasonic

```text
Sensor power controlled by:
SENSOR_PWR_PIN

Startup delay:
~50 ms

Normal measurement:
10 readings

Normal inter-reading delay:
~5 ms
```

## Low-water confirmation

```text
Initial threshold:
< 6%

Confirmation:
10 readings

Confirmation delay:
100 ms between readings
```

These values were already tested in a real working environment and should not be casually changed.

---

# 69. Separation of Concerns

The system should follow this principle:

```text
TX
=
Measurement + Power Management + ESP-NOW + Tank Feedback

RX
=
ESP-NOW + Wi-Fi Gateway + MQTT + Motor Control

Raspberry Pi
=
Storage + Dashboard + Configuration + Analysis
```

Do not make the TX responsible for:

- Wi-Fi.
- HTTP.
- MQTT.
- Dashboard logic.
- Database.
- Time synchronization.
- Direct mains motor control.

Do not make the Raspberry Pi responsible for direct ESP-NOW communication.

The RX is the bridge.

---

# 70. Final End-to-End Telemetry Flow

```text
                    SENSOR DATA

Water Tank
    ↓
Ultrasonic Sensor
    ↓
Xiao ESP32-C3 TX
    ↓
Calculate:
    - distance
    - water %
    - battery voltage
    - battery %
    - motor state
    ↓
ESP-NOW
    ↓
ESP32-S3 RX
    ↓
ACK → TX
    ↓
MQTT
    ↓
Raspberry Pi
    ↓
Database
    ↓
Landing / Graph Dashboard
```

---

# 71. Final End-to-End Configuration Flow

```text
                    CONFIGURATION

User
    ↓
Config Page
    ↓
Raspberry Pi Backend
    ↓
MQTT
    ↓
ESP32-S3 RX
    ↓
Pending Configuration
    ↓
Wait for TX's next wake
    ↓
ESP-NOW
    ↓
Xiao ESP32-C3 TX
    ↓
Validate
    ↓
NVS
    ↓
CONFIG_ACK
    ↓
RX
    ↓
Pi
    ↓
Config Page shows synchronized
```

---

# 72. Final End-to-End Motor Control Flow

```text
                AUTOMATIC FILLING

Water level low
       ↓
RX decides motor should start
       ↓
Relay ON
       ↓
Motor ON
       ↓
RX → TX: MOTOR_ON
       ↓
TX enters motor-monitoring mode
       ↓
Frequent measurements
       ↓
Tank fills
       ↓
TX detects FULL
       ↓
TX → RX: FULL_DETECTED
       ↓
RX → Relay OFF
       ↓
Motor OFF
       ↓
RX → TX: MOTOR_OFF
       ↓
TX returns to normal interval
       ↓
Deep Sleep
```

---

# 73. Final TX State Machine

```text
                    ┌───────────────┐
                    │   DEEP SLEEP  │
                    └───────┬───────┘
                            │
                         Wake
                            │
                            ▼
                    ┌───────────────┐
                    │ INITIALIZE    │
                    └───────┬───────┘
                            │
                            ▼
                    ┌───────────────┐
                    │ READ WATER    │
                    └───────┬───────┘
                            │
                            ▼
                    ┌───────────────┐
                    │ READ BATTERY  │
                    └───────┬───────┘
                            │
                            ▼
                    ┌───────────────┐
                    │ BUILD PACKET  │
                    └───────┬───────┘
                            │
                            ▼
                    ┌───────────────┐
                    │ SEND ESP-NOW  │
                    └───────┬───────┘
                            │
                     ACK received?
                       /          \
                     YES           NO
                      │             │
                      │          Retry
                      │             │
                      │        retry limit?
                      │             │
                      │             ▼
                      │           Sleep
                      │
                      ▼
                Check config
                      │
                      ▼
                Check motor state
                  /          \
              MOTOR OFF     MOTOR ON
                 │             │
                 ▼             ▼
          NORMAL SLEEP    MOTOR MONITOR
                               │
                               ▼
                         Short Deep Sleep
                               │
                               ▼
                            Wake Again
                               │
                               ▼
                          Measure Water
                               │
                         Full detected?
                           /        \
                         NO          YES
                         │            │
                         ▼            ▼
                    Sleep again   FULL_DETECTED
                                      │
                                      ▼
                                     RX
                                      │
                                  MOTOR OFF
                                      │
                                      ▼
                                 MOTOR_OFF
                                      │
                                      ▼
                              NORMAL SLEEP
```

---

# 74. Final RX State Machine

```text
                 ┌──────────────────┐
                 │    RX RUNNING    │
                 └────────┬─────────┘
                          │
             ┌────────────┴────────────┐
             │                         │
             ▼                         ▼
       ESP-NOW event              Wi-Fi/MQTT event
             │                         │
             ▼                         ▼
        SENSOR_DATA              CONFIG_SET
             │                         │
             ▼                         ▼
           ACK                    Validate
             │                         │
             ▼                         ▼
       Update state              Store pending
             │                         │
             ▼                         │
        Motor logic                   │
             │                         │
             ▼                         │
          MQTT ◄───────────────────────┘
             │
             ▼
       Raspberry Pi
```

Motor-specific branch:

```text
SENSOR_DATA / FULL_DETECTED
             │
             ▼
       Motor Controller
             │
       ┌─────┴─────┐
       │           │
    Full         Timeout
       │           │
       └─────┬─────┘
             ▼
        Relay OFF
             │
             ▼
        MOTOR_OFF
             │
             ▼
            TX
```

---

# 75. Dashboard Page Summary

## Page 1: Landing

Purpose:

**Current system status**

Shows:

- Water level.
- Distance.
- Battery.
- Battery voltage.
- Motor state.
- TX status.
- Last update.
- Config synchronization.
- Current filling state.

---

## Page 2: Graph

Purpose:

**Historical system behavior**

Shows:

- Water percentage over time.
- Distance over time.
- Battery percentage.
- Battery voltage.
- Motor activity/runtime.

---

## Page 3: Config

Purpose:

**Control system parameters**

Shows/controls:

- Tank top distance.
- Tank bottom distance.
- Normal measurement interval.
- Motor monitoring interval.
- Low-water threshold.
- Maximum motor runtime.
- Config versions.
- Synchronization status.

---

# 76. Development Phases

Do NOT build the entire system at once.

## Phase 1: TX sensing

Port the existing validated code to Xiao ESP32-C3.

Verify:

- Battery ADC.
- Battery calibration.
- Ultrasonic measurement.
- Sensor power switching.
- Low-water fault detection.

No networking yet.

---

## Phase 2: TX deep sleep

Add:

- Wake timer.
- Sensor measurement.
- Battery measurement.
- Deep sleep.

Measure actual current consumption.

Goal:

```text
Wake → measure → sleep
```

works repeatedly.

---

## Phase 3: Basic ESP-NOW

Build:

```text
TX → RX
```

with:

```text
sequence
water %
distance
battery %
battery voltage
```

No configuration yet.

---

## Phase 4: ACK/retry

Add:

```text
TX → DATA
RX → ACK
```

and retry logic.

Test by deliberately moving RX out of range or turning it off.

---

## Phase 5: RX Wi-Fi

Add Wi-Fi to RX while keeping ESP-NOW active.

Verify:

```text
TX → ESP-NOW → RX → Wi-Fi
```

Do not introduce MQTT yet.

---

## Phase 6: Raspberry Pi MQTT

Add:

```text
RX → MQTT → Pi
```

Verify telemetry appears correctly.

---

## Phase 7: Database + three-page dashboard

Build:

```text
Landing Page
Graph Page
Config Page
```

Store:

- Water level.
- Distance.
- Battery.
- Timestamps.
- Configuration version.
- Motor state.
- Status.

---

## Phase 8: Configuration

Implement:

```text
Dashboard
→ Pi
→ RX
→ ESP-NOW
→ TX
→ NVS
```

with version numbers.

---

## Phase 9: Motor control without real pump

Test relay logic with a safe test load or appropriate isolated test setup.

Verify:

```text
MOTOR_ON
MOTOR_OFF
FULL_DETECTED
MAX_RUNTIME
```

before connecting the actual motor.

---

## Phase 10: Automatic filling

Connect the real control system after the software safety behavior has been validated.

Test:

- Low-water start.
- Filling monitoring.
- FULL detection.
- Relay OFF.
- Motor OFF confirmation.
- Maximum runtime timeout.

---

## Phase 11: Failure testing

Test:

- RX powered off.
- Pi powered off.
- Wi-Fi disconnected.
- TX temporarily out of range.
- Sensor failure.
- Invalid configuration.
- Reboot RX.
- Reboot TX.
- Power loss during configuration.
- TX failure while motor is ON.
- FULL packet loss.
- Long-term deep-sleep cycles.
- Relay/controller restart.

---

## Phase 12: Optimization

Only after the system is stable:

- Optimize TX wake time.
- Optimize ESP-NOW retries.
- Optimize sensor power.
- Optimize battery measurement.
- Optimize motor monitoring interval.
- Improve dashboard.
- Add alerts.
- Add security.

---

# 77. Motor-Control Testing Checklist

Before connecting the real motor:

```text
[ ] Relay defaults OFF
[ ] RX reboot leaves motor OFF
[ ] TX reboot does not unexpectedly start motor
[ ] MOTOR_ON reaches TX
[ ] MOTOR_OFF reaches TX
[ ] FULL_DETECTED stops relay
[ ] Maximum runtime stops relay
[ ] Missing TX packets cannot cause indefinite motor operation
[ ] Invalid sensor readings do not cause unsafe motor behavior
[ ] Invalid configuration is rejected
[ ] Configuration persists after TX reboot
[ ] Motor state is visible on dashboard
```

---

# 78. Initial Configuration Example

A reasonable initial test configuration:

```text
Water Top Level:
10 cm

Water Bottom Level:
150 cm

Normal Measurement Interval:
300 seconds

Motor Monitoring Interval:
15 seconds

Low Water Threshold:
20%

Maximum Motor Runtime:
3600 seconds
```

These values must be calibrated against the actual tank and pump behavior.

---

# 79. Future Multi-TX Support

The architecture should not assume there will only ever be one tank.

Use:

```text
device_id = tx01
```

from the beginning.

Later:

```text
tx01
tx02
tx03
```

can potentially communicate with one RX gateway if ESP-NOW peer/range limitations are handled appropriately.

Dashboard can eventually show:

```text
Tanks

TX01   81%   Online   Motor OFF
TX02   42%   Online   Motor ON
TX03   93%   Offline
```

This is a future capability, not an initial requirement.

---

# 80. Future Features

The architecture leaves room for:

- Multiple tanks.
- Pump control.
- Automatic pump start/stop.
- Leak detection.
- Abnormal drain detection.
- Water consumption estimation.
- Solar charging history.
- Battery health tracking.
- Low-water alerts.
- TX offline alerts.
- Telegram notifications.
- Mobile-friendly dashboard.
- Remote configuration.
- Sensor calibration mode.
- Manual measurement request.
- Firmware update mechanism.
- Stronger authentication.
- ESP-NOW encryption.
- More advanced motor safety logic.

---

# 81. Important Design Principles

### Principle 1: Keep the TX simple

The TX is the energy-constrained device.

### Principle 2: Keep the RX always available

The RX is mains powered and should act as a permanent gateway.

### Principle 3: Do not unnecessarily switch radio modes

Wi-Fi and ESP-NOW should coexist on the same channel.

### Principle 4: Preserve known-good sensing code

The existing battery and ultrasonic logic has already been tested in the real environment.

### Principle 5: Separate measurement from communication

Sensor code should not know about MQTT or the dashboard.

### Principle 6: Separate gateway from dashboard

The RX should not become the database.

### Principle 7: RX owns motor control

The TX measures tank level. RX controls the relay.

### Principle 8: Use configuration versions

This makes configuration synchronization deterministic.

### Principle 9: Use ACKs where they provide real value

Use ESP-NOW delivery status for routine telemetry where appropriate.

Use application-level ACKs, sequence numbers, and retries for important commands and configuration changes.

### Principle 10: Never let bad configuration destroy the working configuration

Validate before applying.

### Principle 11: Motor control must fail toward OFF

A communication or sensor problem must not result in an indefinitely running pump.

### Principle 12: Optimize power only after correctness

First build a reliable system. Then reduce active time/current.

---

# 82. Final Architecture

```text
                         ┌─────────────────────────────┐
                         │        RASPBERRY PI 5       │
                         │                             │
                         │  ┌─────────┐ ┌───────────┐ │
                         │  │ Landing │ │   Graph   │ │
                         │  └─────────┘ └───────────┘ │
                         │  ┌─────────┐               │
                         │  │ Config  │               │
                         │  └─────────┘               │
                         │                             │
                         │ Backend / API               │
                         │ MQTT                        │
                         │ SQLite                      │
                         └──────────────┬──────────────┘
                                        │
                                      Wi-Fi
                                        │
                                        ▼
                         ┌─────────────────────────────┐
                         │         ESP32-S3 RX         │
                         │                             │
                         │ Wi-Fi                       │
                         │ ESP-NOW                     │
                         │ MQTT Gateway                │
                         │ Motor Controller            │
                         │ Relay Output                │
                         └──────────────┬──────────────┘
                                        │
                         ┌──────────────┴──────────────┐
                         │                             │
                      Relay                       ESP-NOW
                         │                             │
                         ▼                             ▼
                       MOTOR                    Xiao ESP32-C3
                                                       │
                                             ┌─────────┴─────────┐
                                             │                   │
                                        Ultrasonic         Battery/Solar
                                             │                   │
                                             └─────────┬─────────┘
                                                       │
                                                       ▼
                                                   WATER TANK
```

---

# 83. Final Operating Model

## Configuration Session

```text
Dashboard:
Open Config
↓
Start configuration session

TX:
Enter CONFIG_MODE
↓
Remain awake
↓
Measure
↓
Send live telemetry
↓
Wait / repeat

Dashboard:
Edit configuration
↓
Apply & Preview
↓
TX applies temporary RAM configuration
↓
Live water % updates

Save?
├── YES → CONFIG_SAVE → NVS
└── NO  → CONFIG_SESSION_END → discard temporary config

TX:
Return to normal low-power operation
```

The configuration session is intentionally temporary. The TX remains awake only while the session is active.

## Motor OFF

```text
TX:
Wake every ~5 min
↓
Measure
↓
Send telemetry
↓
Deep sleep
```

## Motor ON

```text
RX:
Relay ON
↓
MOTOR_ON → TX

TX:
Remain awake
↓
Measure every ~15 sec
↓
Send telemetry
↓
Wait
↓
Repeat

No deep sleep while motor is ON and filling is active.

Until:
water_percentage >= 100%
↓
FULL_DETECTED
↓
RX:
Relay OFF
↓
MOTOR_OFF → TX
↓
TX:
Confirm/observe motor OFF
↓
Verify water_percentage >= 100%
↓
Normal 5-minute deep-sleep cycle
```

The 15-second value is the motor-monitoring measurement/reporting interval. It is **not** a deep-sleep interval.

## Motor timeout

```text
Motor ON
↓
Maximum runtime exceeded
↓
RX:
Relay OFF
↓
System records safety timeout
↓
TX returns to normal monitoring when possible
```

---

# 84. Project Baseline

This document is the baseline architecture for Water Tank Monitor V2.

Any future code changes should preserve these core boundaries unless there is a documented reason to change them:

```text
TX:
    Sense
    Calculate
    Communicate
    Monitor tank during filling
    Sleep

RX:
    Receive
    Acknowledge where required
    Gateway
    Forward
    Deliver configuration
    Manage configuration sessions
    Control motor

Raspberry Pi:
    Store
    Display
    Configure
    Manage configuration sessions
    Analyze
```

The three dashboard pages are:

```text
1. Landing
   Current live status

2. Graph
   Historical behavior

3. Config
   Runtime configuration, live preview, and synchronization
```

The system's primary motor/power-state rules are:

```text
When the tank is full → motor OFF.

While motor is ON and the tank is filling:
    TX remains awake.
    TX does not enter deep sleep.

TX may return to normal deep sleep only when:
    water_percentage >= 100%
    AND
    motor_status == OFF

If the expected FULL event fails:
    maximum motor runtime must still force motor OFF.
```

Security is intentionally deferred.

Reliability, power efficiency, sensor correctness, clean communication boundaries, and safe motor control are the initial priorities.

---

# 85. Next Implementation Step

Before writing the complete firmware, finalize:

1. Exact ESP-NOW packet structures.
2. ACK timing and retry behavior for telemetry versus important commands.
3. TX wake/sleep sequence.
4. Configuration-session start/end behavior.
5. Temporary configuration / live-preview behavior.
6. Save/discard configuration behavior.
7. Motor ON/OFF state transitions.
8. FULL_DETECTED behavior.
9. Maximum motor runtime behavior.
10. Configuration packet format.
11. NVS configuration layout.
12. MQTT topic structure.
13. Raspberry Pi API/database schema.
14. Three dashboard page layouts.
15. RX Wi-Fi channel strategy.
16. Exact ESP32-C3 and ESP32-S3 pin assignments.
17. Relay module interface and fail-safe behavior.
18. Exact ultrasonic library/API used by the current hardware.

Once these are fixed, implementation can proceed module-by-module rather than trying to build the whole system as one large sketch.

# 86. Scripting Preferences

## General

- All ESP32 scripts must be flashable using the Arduino IDE.
- Define all GPIO pins and hardware-related pin assignments at the top of the relevant script.
- Keep the code understandable and well structured.
- Use clear, descriptive functions instead of putting all logic inside `setup()` or `loop()`.
- Prefer modular functions and simple control flow.
- Preserve existing, tested hardware logic unless there is a specific reason to change it.
- Avoid unnecessary complexity or frameworks that make Arduino IDE flashing difficult.

## Reuse These Existing Pin Definitions

Unless there is a specific hardware change, reuse these existing pin assignments:

- for tx script:
```cpp
const int echoPin = D2;          // BROWN
const int trigPin = D3;          // ORANGE

#define SENSOR_PWR_PIN D10
#define SETUP_TRIG     D4
#define SETUP_LED      LED_BUILTIN

#define ANALOG_IN_PIN  A0
```
```cpp
- for rx script:
    #define MOTOR_RELAY 17
    #define STATUS_LED 18 // lights up when motor is turned on
```
