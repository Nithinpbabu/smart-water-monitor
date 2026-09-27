#include "wifi_manager.h"

RxWiFiManager rxWiFi;

RxWiFiManager::RxWiFiManager() 
    : _ssid(NULL), _password(NULL), _lastReconnectAttempt(0), _timeSynced(false) {}

void RxWiFiManager::begin(const char* ssid, const char* password) {
    _ssid = ssid;
    _password = password;
    
    WiFi.mode(WIFI_STA);
    
    if (_ssid != NULL && strlen(_ssid) > 0) {
        Serial.printf("[WiFi] Connecting to Wi-Fi SSID: '%s'...\n", _ssid);
        WiFi.begin(_ssid, _password);
    } else {
        Serial.println("[WiFi] No SSID provided. Running ESP-NOW in standalone AP/STA mode.");
    }
}

void RxWiFiManager::initTimeSync() {
    // GMT Offset for IST (+5:30) = 5.5 * 3600 = 19800 seconds
    configTime(19800, 0, "pool.ntp.org", "time.nist.gov");
    Serial.println("[WiFi NTP] Configured NTP time sync for IST (GMT +5:30)...");
}

bool RxWiFiManager::getLocalTimeStruct(struct tm* timeinfo) const {
    if (!getLocalTime(timeinfo)) {
        return false;
    }
    return (timeinfo->tm_year > (2020 - 1900)); // Ensure year is valid (after 2020)
}

bool RxWiFiManager::isTimeSynced() const {
    struct tm timeinfo;
    return getLocalTimeStruct(&timeinfo);
}

uint32_t RxWiFiManager::getTodayDateInt() const {
    struct tm timeinfo;
    if (!getLocalTimeStruct(&timeinfo)) {
        return 0; // Return 0 if time is not synced yet
    }
    // Format YYYYMMDD as integer, e.g., (2026 * 10000) + (9 * 100) + 23 = 20260923
    uint32_t year = timeinfo.tm_year + 1900;
    uint32_t month = timeinfo.tm_mon + 1;
    uint32_t day = timeinfo.tm_mday;
    return (year * 10000) + (month * 100) + day;
}

String RxWiFiManager::getFormattedTime() const {
    struct tm timeinfo;
    if (!getLocalTimeStruct(&timeinfo)) {
        return "TIME_NOT_SET";
    }
    char timeStr[32];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S IST", &timeinfo);
    return String(timeStr);
}

void RxWiFiManager::update() {
    if (_ssid == NULL || strlen(_ssid) == 0) return;
    
    if (WiFi.status() == WL_CONNECTED) {
        if (!_timeSynced) {
            initTimeSync();
            _timeSynced = true;
        }
    } else {
        _timeSynced = false;
        unsigned long now = millis();
        if (now - _lastReconnectAttempt > 10000) {
            _lastReconnectAttempt = now;
            Serial.printf("[WiFi] Reconnecting to '%s'...\n", _ssid);
            WiFi.disconnect();
            WiFi.begin(_ssid, _password);
        }
    }
}
