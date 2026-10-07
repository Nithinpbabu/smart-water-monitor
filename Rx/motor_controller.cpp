#include "motor_controller.h"
#include "mqtt_client.h"
#include "espnow_gateway.h"

MotorController motorCtrl;

MotorController::MotorController() 
    : _motorOn(false), _startTimeMs(0), _maxRuntimeSec(3600), _lastTelemetryRecvMs(0), _initialSyncPending(false) {}

void MotorController::begin() {
    pinMode(MOTOR_RELAY, OUTPUT);
    digitalWrite(MOTOR_RELAY, LOW); // Default OFF
    
    pinMode(STATUS_LED, OUTPUT);
    digitalWrite(STATUS_LED, LOW);
    
    _lastTelemetryRecvMs = millis();
    _initialSyncPending = false;
    
    Serial.println("[MotorCtrl] Initialized Motor Relay (GPIO 17) & Status LED (GPIO 18).");
}

void MotorController::turnOn(uint32_t maxRuntimeSec, const char* reason) {
    _motorOn = true;
    _startTimeMs = millis();
    _lastTelemetryRecvMs = millis();
    _initialSyncPending = true;
    
    if (maxRuntimeSec > 0) {
        _maxRuntimeSec = maxRuntimeSec;
    }
    
    digitalWrite(MOTOR_RELAY, HIGH);
    digitalWrite(STATUS_LED, HIGH);
    
    Serial.printf("[MotorCtrl] MOTOR TURNED ON! Reason: '%s'. Max Runtime: %u sec\n", 
                  reason ? reason : "UNSPECIFIED", _maxRuntimeSec);
}

void MotorController::turnOff(const char* reason) {
    uint32_t runTimeSec = getMotorRuntimeSeconds();
    _motorOn = false;
    _initialSyncPending = false;
    digitalWrite(MOTOR_RELAY, LOW);
    digitalWrite(STATUS_LED, LOW);
    
    Serial.printf("[MotorCtrl] MOTOR TURNED OFF! Reason: '%s'. Total Active Duration: %u sec\n", 
                  reason ? reason : "UNSPECIFIED", runTimeSec);
}

uint32_t MotorController::getMotorRuntimeSeconds() const {
    if (!_motorOn) return 0;
    return (millis() - _startTimeMs) / 1000;
}

void MotorController::handleFullDetected() {
    turnOff("TANK_100_FULL_CUTOFF");
}

void MotorController::updateTelemetryTimestamp() {
    _lastTelemetryRecvMs = millis();
    if (_initialSyncPending) {
        _initialSyncPending = false;
        Serial.println("[MotorCtrl] Initial telemetry sync established with Tx. Active 45s Watchdog engaged.");
    }
}

void MotorController::update() {
    if (!_motorOn) return;
    
    // 1. Safety max runtime check
    uint32_t elapsedSec = getMotorRuntimeSeconds();
    if (elapsedSec >= _maxRuntimeSec) {
        Serial.printf("[MotorCtrl] SAFETY CUT-OFF TRIGGERED! Exceeded max allowed runtime of %u seconds.\n", _maxRuntimeSec);
        turnOff("SAFETY_MAX_RUNTIME_EXCEEDED");
        rxMqtt.publishMotorEvent("SAFETY_CUTOFF", "OFF", "SAFETY_MAX_RUNTIME_EXCEEDED");
        return;
    }
    
    // 2. Rx Telemetry Watchdog Timeout check
    uint32_t nowMs = millis();
    uint32_t elapsedTelemetryMs = nowMs - _lastTelemetryRecvMs;
    
    if (_initialSyncPending) {
        // Initial sync grace window: normalSleepIntervalSeconds + 15s
        uint32_t graceThresholdMs = (espNowGateway.getActiveConfig().normalSleepIntervalSeconds + 15) * 1000;
        if (elapsedTelemetryMs >= graceThresholdMs) {
            Serial.printf("[MotorCtrl] WATCHDOG INITIAL SYNC TIMEOUT! No telemetry received for %u sec after motor start.\n", 
                          elapsedTelemetryMs / 1000);
            turnOff("SAFETY_CUTOFF_INITIAL_SYNC_TIMEOUT");
            rxMqtt.publishMotorEvent("SAFETY_CUTOFF", "OFF", "INITIAL_SYNC_TIMEOUT");
        }
    } else {
        // Active filling watchdog: strict 45 seconds (3x 10s monitoring interval)
        if (elapsedTelemetryMs >= 45000) {
            Serial.printf("[MotorCtrl] WATCHDOG TELEMETRY TIMEOUT! No telemetry received for %u sec during active fill.\n", 
                          elapsedTelemetryMs / 1000);
            turnOff("SAFETY_CUTOFF_TELEMETRY_WATCHDOG_TIMEOUT");
            rxMqtt.publishMotorEvent("SAFETY_CUTOFF", "OFF", "TELEMETRY_WATCHDOG_TIMEOUT");
        }
    }
}
