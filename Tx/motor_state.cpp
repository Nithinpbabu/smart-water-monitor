#include "motor_state.h"
#include "config.h"

TxStateManager txState;

// Persist operating mode in ESP32 RTC memory so reboots during OTA stay in OTA mode!
RTC_DATA_ATTR static TxOperatingMode rtcOperatingMode = MODE_NORMAL;

TxStateManager::TxStateManager() 
    : _motorStatus(MOTOR_STATUS_OFF), _lastSessionActivityMs(0), _consecutiveFailures(0) {}

TxOperatingMode TxStateManager::getCurrentMode() const {
    return rtcOperatingMode;
}

void TxStateManager::setMotorStatus(uint8_t status) {
    _motorStatus = status;
    
    if (_motorStatus == MOTOR_STATUS_ON) {
        if (rtcOperatingMode != MODE_CONFIG_SESSION && rtcOperatingMode != MODE_OTA) {
            rtcOperatingMode = MODE_MOTOR_FILLING;
        }
        Serial.println("[TxState] Motor turned ON! Entering MOTOR_FILLING mode (NO deep sleep).");
    } else {
        if (rtcOperatingMode == MODE_MOTOR_FILLING) {
            rtcOperatingMode = MODE_NORMAL;
            Serial.println("[TxState] Motor turned OFF! Returning to NORMAL deep sleep mode.");
        }
    }
}

void TxStateManager::startConfigSession() {
    if (rtcOperatingMode == MODE_OTA) return; // Do not override OTA mode
    rtcOperatingMode = MODE_CONFIG_SESSION;
    _lastSessionActivityMs = millis();
    _consecutiveFailures = 0;
    Serial.println("[TxState] Configuration session STARTED. Staying awake for interactive preview (5-min safety timeout active).");
}

void TxStateManager::endConfigSession() {
    if (rtcOperatingMode == MODE_OTA) return;
    _consecutiveFailures = 0;
    if (_motorStatus == MOTOR_STATUS_ON) {
        rtcOperatingMode = MODE_MOTOR_FILLING;
        Serial.println("[TxState] Configuration session ENDED. Motor is ON -> Entering MOTOR_FILLING mode.");
    } else {
        rtcOperatingMode = MODE_NORMAL;
        Serial.println("[TxState] Configuration session ENDED. Resuming NORMAL deep sleep mode.");
    }
}

void TxStateManager::startOTAMode() {
    rtcOperatingMode = MODE_OTA;
    Serial.println("[TxState] OTA Mode STARTED & Persisted in RTC Memory. Deep sleep suspended for wireless firmware update.");
}

void TxStateManager::clearOTAMode() {
    rtcOperatingMode = MODE_NORMAL;
    Serial.println("[TxState] OTA Mode CLEARED in RTC Memory.");
}

void TxStateManager::touchSessionActivity() {
    _lastSessionActivityMs = millis();
    _consecutiveFailures = 0;
}

void TxStateManager::recordTransmissionResult(bool success) {
    if (success) {
        _consecutiveFailures = 0;
        _lastSessionActivityMs = millis();
    } else {
        _consecutiveFailures++;
        if (rtcOperatingMode == MODE_CONFIG_SESSION) {
            Serial.printf("[TxState] Live session transmission failed (%u/5 consecutive failures)\n", _consecutiveFailures);
        }
    }
}

bool TxStateManager::checkSessionTimeout() {
    if (rtcOperatingMode != MODE_CONFIG_SESSION) {
        return false;
    }
    
    // Safety 1: Check 5 consecutive transmission failures
    if (_consecutiveFailures >= 5) {
        Serial.println("[TxState] Configuration session TIMEOUT (5 consecutive transmission failures). Auto-exiting to save battery!");
        configMgr.clearPreviewConfig();
        endConfigSession();
        return true;
    }
    
    // Safety 2: Check 5-minute inactivity timer (300,000 ms)
    uint32_t elapsedMs = millis() - _lastSessionActivityMs;
    if (elapsedMs >= 300000) { // 5 minutes
        Serial.printf("[TxState] Configuration session TIMEOUT (%u s inactive). Auto-exiting to save battery!\n", elapsedMs / 1000);
        configMgr.clearPreviewConfig();
        endConfigSession();
        return true;
    }
    
    return false;
}

bool TxStateManager::updateState(uint8_t waterPercentage) {
    if (_motorStatus == MOTOR_STATUS_ON && waterPercentage >= 100) {
        Serial.println("[TxState] FULL DETECTED! Water percentage >= 100%");
        return true; // FULL_DETECTED event should be sent
    }
    return false;
}
