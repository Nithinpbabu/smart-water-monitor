#ifndef RX_MOTOR_CONTROLLER_H
#define RX_MOTOR_CONTROLLER_H

#include <Arduino.h>

#define MOTOR_RELAY 17
#define STATUS_LED  18

class MotorController {
public:
    MotorController();
    void begin();
    
    void turnOn(uint32_t maxRuntimeSec, const char* reason);
    void turnOff(const char* reason);
    
    bool isMotorOn() const { return _motorOn; }
    uint32_t getMotorRuntimeSeconds() const;
    uint32_t getMaxRuntimeSeconds() const { return _maxRuntimeSec; }
    void setMaxRuntimeSeconds(uint32_t sec) { _maxRuntimeSec = sec; }
    
    void handleFullDetected();
    void updateTelemetryTimestamp();
    void update();

private:
    bool _motorOn;
    uint32_t _startTimeMs;
    uint32_t _maxRuntimeSec;
    uint32_t _lastTelemetryRecvMs;
    bool _initialSyncPending;
};

extern MotorController motorCtrl;

#endif // RX_MOTOR_CONTROLLER_H
