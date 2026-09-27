#ifndef RX_PRE_PEAK_FILL_H
#define RX_PRE_PEAK_FILL_H

#include <Arduino.h>
#include <Preferences.h>
#include "wifi_manager.h"
#include "motor_controller.h"

class PrePeakFillManager {
public:
    PrePeakFillManager();
    void begin();
    
    // Core evaluation method called on every loop / telemetry update
    void checkAndTrigger(uint8_t currentWaterPct, uint32_t maxRuntimeSec);
    
    // Warmup window evaluation methods (Warmup = 2 * normalSleepIntervalSeconds)
    bool isWarmupActive(uint32_t normalSleepSec) const;
    uint16_t getWarmupMinutes(uint32_t normalSleepSec) const;
    
    // Config setters & getters
    void setEnabled(bool enabled);
    bool isEnabled() const { return _enabled; }
    
    void setPeakTime(uint8_t hour, uint8_t minute);
    uint8_t getPeakHour() const { return _peakHour; }
    uint8_t getPeakMinute() const { return _peakMinute; }
    
    void setWindowMinutes(uint8_t minutes);
    uint8_t getWindowMinutes() const { return _windowMinutes; }
    
    void setWaterThreshold(uint8_t thresholdPct);
    uint8_t getWaterThreshold() const { return _waterThreshold; }
    
    uint32_t getLastCompletedDate() const { return _lastCompletedDate; }
    
    // Diagnostic & testing helpers
    bool isWindowActive() const;
    void resetCompletedDate();
    bool simulateTrigger(uint8_t currentWaterPct, uint32_t maxRuntimeSec);
    String getStatusString() const;

private:
    bool     _enabled;
    uint8_t  _peakHour;            // Default: 17 (5 PM)
    uint8_t  _peakMinute;          // Default: 30 (5:30 PM)
    uint8_t  _windowMinutes;       // Default: 15 min
    uint8_t  _waterThreshold;      // Default: 60%
    
    uint32_t _lastCompletedDate;   // Stored in NVS as YYYYMMDD integer
    Preferences _prefs;
    
    void saveCompletedDate(uint32_t dateInt);
};

extern PrePeakFillManager prePeakFill;

#endif // RX_PRE_PEAK_FILL_H
