#ifndef TX_MOTOR_STATE_H
#define TX_MOTOR_STATE_H

#include <Arduino.h>
#include "protocol.h"

enum TxOperatingMode {
    MODE_NORMAL,
    MODE_MOTOR_FILLING,
    MODE_CONFIG_SESSION,
    MODE_OTA
};

class TxStateManager {
public:
    TxStateManager();
    
    TxOperatingMode getCurrentMode() const;
    uint8_t getMotorStatus() const { return _motorStatus; }
    
    void setMotorStatus(uint8_t status);
    void startConfigSession();
    void endConfigSession();
    void startOTAMode();
    void clearOTAMode();
    
    void touchSessionActivity();
    void recordTransmissionResult(bool success);
    bool checkSessionTimeout();
    
    bool updateState(uint8_t waterPercentage);
    
private:
    uint8_t _motorStatus; // MOTOR_STATUS_OFF or MOTOR_STATUS_ON
    
    uint32_t _lastSessionActivityMs;
    uint8_t _consecutiveFailures;
};

extern TxStateManager txState;

#endif // TX_MOTOR_STATE_H
