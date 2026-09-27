#include "motor_controller.h"

MotorController motorCtrl;

MotorController::MotorController() 
    : _motorOn(false), _startTimeMs(0), _maxRuntimeSec(3600) {}

void MotorController::begin() {
    pinMode(MOTOR_RELAY, OUTPUT);
    digitalWrite(MOTOR_RELAY, LOW); // Default OFF
    
    pinMode(STATUS_LED, OUTPUT);
    digitalWrite(STATUS_LED, LOW);
    
    Serial.println("[MotorCtrl] Initialized Motor Relay (GPIO 17) & Status LED (GPIO 18).");
}

void MotorController::turnOn(uint32_t maxRuntimeSec, const char* reason) {
    _motorOn = true;
    _startTimeMs = millis();
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

void MotorController::update() {
    if (!_motorOn) return;
    
    // Safety max runtime check
    uint32_t elapsedSec = getMotorRuntimeSeconds();
    if (elapsedSec >= _maxRuntimeSec) {
        Serial.printf("[MotorCtrl] SAFETY CUT-OFF TRIGGERED! Exceeded max allowed runtime of %u seconds.\n", _maxRuntimeSec);
        turnOff("SAFETY_MAX_RUNTIME_EXCEEDED");
    }
}
