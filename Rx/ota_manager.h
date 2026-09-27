#ifndef RX_OTA_MANAGER_H
#define RX_OTA_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoOTA.h>

class RxOTAManager {
public:
    RxOTAManager();
    
    bool begin();
    void update();
    void startOTAMode();
    void endOTAMode();
    
    bool isOTAActive() const { return _isOTAActive; }

private:
    bool _isOTAActive;
    uint32_t _otaStartTimeMs;
};

extern RxOTAManager rxOTA;

#endif // RX_OTA_MANAGER_H
