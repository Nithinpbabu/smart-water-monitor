#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <Arduino.h>

#pragma pack(push, 1)

enum MessageType : uint8_t {
    MSG_SENSOR_DATA           = 0x01,
    MSG_ACK                   = 0x02,
    MSG_CONFIG_REQUEST        = 0x03,
    MSG_CONFIG_RESPONSE       = 0x04,
    MSG_CONFIG_ACK            = 0x05,
    MSG_CONFIG_SESSION_START  = 0x06,
    MSG_CONFIG_SESSION_ACK    = 0x07,
    MSG_CONFIG_PREVIEW        = 0x08,
    MSG_CONFIG_PREVIEW_ACK    = 0x09,
    MSG_CONFIG_SAVE           = 0x0A,
    MSG_CONFIG_SAVE_ACK       = 0x0B,
    MSG_CONFIG_SESSION_END    = 0x0C,
    MSG_CONFIG_SESSION_END_ACK= 0x0D,
    MSG_MOTOR_ON              = 0x0E,
    MSG_MOTOR_OFF             = 0x0F,
    MSG_FULL_DETECTED         = 0x10,
    MSG_OTA_START             = 0x11,
    MSG_OTA_END               = 0x12
};

enum SensorStatusCode : uint8_t {
    SENSOR_STATUS_OK          = 0,
    SENSOR_STATUS_ERROR       = 1,
    SENSOR_STATUS_GLITCH_FIX  = 2
};

enum MotorStatusCode : uint8_t {
    MOTOR_STATUS_OFF          = 0,
    MOTOR_STATUS_ON           = 1
};

struct SensorPacket {
    uint8_t  messageType;          // MSG_SENSOR_DATA
    uint32_t sequence;             // Tx sequence count
    uint32_t configVersion;        // Tx active config version
    uint16_t waterDistanceCm;      // Measured ultrasonic distance in cm
    uint8_t  waterPercentage;       // Calculated 0..100%
    uint16_t batteryVoltageMv;     // Raw battery voltage in mV (e.g. 3870)
    uint8_t  batteryPercentage;    // Calculated 0..100%
    uint8_t  sensorStatus;         // SENSOR_STATUS_OK, etc.
    uint8_t  motorStatus;          // MOTOR_STATUS_OFF or MOTOR_STATUS_ON
};

struct AckPacket {
    uint8_t  messageType;          // MSG_ACK
    uint32_t sequence;             // Echo sequence count
    uint32_t configVersion;        // Current Rx config version
    uint8_t  rxMotorStatus;        // Authoritative Rx motor status (0=OFF, 1=ON)
};

struct ConfigPacket {
    uint8_t  messageType;          // MSG_CONFIG_RESPONSE, MSG_CONFIG_PREVIEW, MSG_CONFIG_SAVE, etc.
    uint32_t configVersion;        // Configuration version integer
    uint16_t waterTopLevelCm;      // Ultrasonic distance to FULL (e.g., 10cm)
    uint16_t waterBottomLevelCm;   // Ultrasonic distance to EMPTY (e.g., 150cm)
    uint32_t normalSleepIntervalSeconds;      // Deep sleep interval in normal mode (e.g., 300s)
    uint32_t motorMonitoringIntervalSeconds; // Reading interval during filling mode (e.g., 15s)
    uint8_t  lowWaterThreshold;    // Threshold % to trigger filling (e.g., 20%)
    uint32_t maxMotorRuntimeSeconds; // Safety max motor runtime in seconds (e.g., 3600s)
};

struct MotorCommandPacket {
    uint8_t  messageType;          // MSG_MOTOR_ON or MSG_MOTOR_OFF
    uint32_t sequence;             // Command sequence
    uint32_t maxMotorRuntimeSeconds; // Enforced max runtime
};

struct FullDetectedPacket {
    uint8_t  messageType;          // MSG_FULL_DETECTED
    uint32_t sequence;             // Sequence number
    uint16_t waterDistanceCm;      // Measured distance
    uint8_t  waterPercentage;       // 100%
};

#pragma pack(pop)

#endif // PROTOCOL_H
