#ifndef RX_WIFI_MANAGER_H
#define RX_WIFI_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>

class RxWiFiManager {
public:
    RxWiFiManager();
    void begin(const char* ssid, const char* password);
    void update();
    
    bool isConnected() const { return WiFi.status() == WL_CONNECTED; }
    uint8_t getChannel() const { return WiFi.channel(); }
    String getLocalIP() const { return WiFi.localIP().toString(); }

    // NTP Time Synchronization (IST GMT+5:30)
    void initTimeSync();
    bool isTimeSynced() const;
    bool getLocalTimeStruct(struct tm* timeinfo) const;
    uint32_t getTodayDateInt() const; // Returns YYYYMMDD (e.g. 20260923)
    String getFormattedTime() const; // Returns "HH:MM:SS"

private:
    const char* _ssid;
    const char* _password;
    unsigned long _lastReconnectAttempt;
    bool _timeSynced;
};

extern RxWiFiManager rxWiFi;

#endif // RX_WIFI_MANAGER_H
