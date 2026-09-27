#include "pre_peak_fill.h"
#include "mqtt_client.h"

PrePeakFillManager prePeakFill;

PrePeakFillManager::PrePeakFillManager()
    : _enabled(true), _peakHour(17), _peakMinute(30), _windowMinutes(15), 
      _waterThreshold(60), _lastCompletedDate(0) {}

void PrePeakFillManager::begin() {
    _prefs.begin("pre_peak", false);
    
    // Load config & NVS last completed date
    _enabled = _prefs.getBool("enabled", true);
    _peakHour = _prefs.getUChar("hour", 17);
    _peakMinute = _prefs.getUChar("min", 30);
    _windowMinutes = _prefs.getUChar("window", 15);
    _waterThreshold = _prefs.getUChar("thresh", 60);
    _lastCompletedDate = _prefs.getUInt("last_date", 0);
    
    _prefs.end();
    
    Serial.printf("[PrePeakFill] Initialized. Status: %s | Time: %02u:%02u (Window: %umin) | Threshold: %u%% | NVS Last Done Date: %u\n",
                  _enabled ? "ENABLED" : "DISABLED",
                  _peakHour, _peakMinute, _windowMinutes, _waterThreshold, _lastCompletedDate);
}

void PrePeakFillManager::saveCompletedDate(uint32_t dateInt) {
    _lastCompletedDate = dateInt;
    _prefs.begin("pre_peak", false);
    _prefs.putUInt("last_date", dateInt);
    _prefs.end();
    
    Serial.printf("[PrePeakFill NVS] Saved completed date YYYYMMDD: %u to NVS Flash.\n", dateInt);
}

void PrePeakFillManager::resetCompletedDate() {
    saveCompletedDate(0);
    Serial.println("[PrePeakFill NVS] Reset NVS completed date flag to 0.");
}

uint16_t PrePeakFillManager::getWarmupMinutes(uint32_t normalSleepSec) const {
    uint32_t warmupSec = 2 * normalSleepSec;
    uint16_t warmupMin = warmupSec / 60;
    if (warmupMin == 0) warmupMin = 1;
    return warmupMin;
}

bool PrePeakFillManager::isWarmupActive(uint32_t normalSleepSec) const {
    if (!_enabled) return false;
    if (motorCtrl.isMotorOn()) return false;
    
    uint32_t todayDate = rxWiFi.getTodayDateInt();
    if (todayDate == 0 || _lastCompletedDate == todayDate) return false;
    
    struct tm timeinfo;
    if (!rxWiFi.getLocalTimeStruct(&timeinfo)) return false;
    
    int currentMinutes = (timeinfo.tm_hour * 60) + timeinfo.tm_min;
    int peakStartMinutes = (_peakHour * 60) + _peakMinute;
    int warmupMinutes = getWarmupMinutes(normalSleepSec);
    int warmupStartMinutes = peakStartMinutes - warmupMinutes;
    
    return (currentMinutes >= warmupStartMinutes && currentMinutes < peakStartMinutes);
}

bool PrePeakFillManager::isWindowActive() const {
    struct tm timeinfo;
    if (!rxWiFi.getLocalTimeStruct(&timeinfo)) {
        return false;
    }
    
    int currentMinutesSinceMidnight = (timeinfo.tm_hour * 60) + timeinfo.tm_min;
    int peakStartMinutes = (_peakHour * 60) + _peakMinute;
    int peakEndMinutes = peakStartMinutes + _windowMinutes;
    
    return (currentMinutesSinceMidnight >= peakStartMinutes && currentMinutesSinceMidnight < peakEndMinutes);
}

void PrePeakFillManager::checkAndTrigger(uint8_t currentWaterPct, uint32_t maxRuntimeSec) {
    if (!_enabled) return;
    if (motorCtrl.isMotorOn()) return; // Motor is already running
    
    uint32_t todayDate = rxWiFi.getTodayDateInt();
    if (todayDate == 0) return; // Time not synced yet
    
    // Check if already completed today
    if (_lastCompletedDate == todayDate) {
        return;
    }
    
    // Check if current time falls within Pre-Peak Fill Window
    if (!isWindowActive()) {
        return;
    }
    
    // Check if water level is below threshold
    if (currentWaterPct < _waterThreshold) {
        Serial.printf("[PrePeakFill] PRE-PEAK FILL TRIGGERED! Time: %s, Water: %u%% (Threshold: < %u%%)\n",
                      rxWiFi.getFormattedTime().c_str(), currentWaterPct, _waterThreshold);
                      
        // 1. Mark completed in NVS FIRST for absolute power-cut resilience
        saveCompletedDate(todayDate);
        
        // 2. Turn ON motor relay with reason "PRE_PEAK_FILL"
        motorCtrl.turnOn(maxRuntimeSec, "PRE_PEAK_FILL");
        
        // 3. Log event over MQTT
        char logMsg[128];
        snprintf(logMsg, sizeof(logMsg), "[PrePeakFill] Triggered at %s | Water: %u%% (<%u%%)", 
                 rxWiFi.getFormattedTime().c_str(), currentWaterPct, _waterThreshold);
        rxMqtt.publishLog("rx01", logMsg);
        rxMqtt.publishMotorEvent("PRE_PEAK_FILL", "ON", "WATER_BELOW_PRE_PEAK_THRESHOLD");
    }
}

bool PrePeakFillManager::simulateTrigger(uint8_t currentWaterPct, uint32_t maxRuntimeSec) {
    Serial.printf("[PrePeakFill SIM] Simulating Pre-Peak Fill trigger with water level: %u%%\n", currentWaterPct);
    
    if (motorCtrl.isMotorOn()) {
        Serial.println("[PrePeakFill SIM] Motor is already ON!");
        return false;
    }
    
    uint32_t todayDate = rxWiFi.getTodayDateInt();
    if (todayDate == 0) todayDate = 20260923; // Fallback date for offline simulation
    
    saveCompletedDate(todayDate);
    motorCtrl.turnOn(maxRuntimeSec, "PRE_PEAK_FILL_SIM");
    rxMqtt.publishMotorEvent("PRE_PEAK_FILL_SIM", "ON", "MANUAL_SIMULATION");
    return true;
}

void PrePeakFillManager::setEnabled(bool enabled) {
    _enabled = enabled;
    _prefs.begin("pre_peak", false);
    _prefs.putBool("enabled", _enabled);
    _prefs.end();
}

void PrePeakFillManager::setPeakTime(uint8_t hour, uint8_t minute) {
    if (hour > 23 || minute > 59) return;
    _peakHour = hour;
    _peakMinute = minute;
    _prefs.begin("pre_peak", false);
    _prefs.putUChar("hour", _peakHour);
    _prefs.putUChar("min", _peakMinute);
    _prefs.end();
}

void PrePeakFillManager::setWindowMinutes(uint8_t minutes) {
    if (minutes == 0) return;
    _windowMinutes = minutes;
    _prefs.begin("pre_peak", false);
    _prefs.putUChar("window", _windowMinutes);
    _prefs.end();
}

void PrePeakFillManager::setWaterThreshold(uint8_t thresholdPct) {
    if (thresholdPct > 100) return;
    _waterThreshold = thresholdPct;
    _prefs.begin("pre_peak", false);
    _prefs.putUChar("thresh", _waterThreshold);
    _prefs.end();
}

String PrePeakFillManager::getStatusString() const {
    char buf[300];
    snprintf(buf, sizeof(buf),
        "Enabled: %s | PeakTime: %02u:%02u IST | Window: %umin | Threshold: %u%% | CurrentTime: %s | WindowActive: %s | NVS LastDoneDate: %u (Today: %u)",
        _enabled ? "YES" : "NO",
        _peakHour, _peakMinute, _windowMinutes, _waterThreshold,
        rxWiFi.getFormattedTime().c_str(),
        isWindowActive() ? "YES" : "NO",
        _lastCompletedDate, rxWiFi.getTodayDateInt()
    );
    return String(buf);
}
