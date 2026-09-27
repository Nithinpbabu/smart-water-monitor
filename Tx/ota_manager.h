#ifndef TX_OTA_MANAGER_H
#define TX_OTA_MANAGER_H

#include <Arduino.h>

class TxOtaManager {
public:
    TxOtaManager();
    void begin(const char* ssid, const char* pass);
    void update();
    
    void startOTAMode();
    void clearOTAMode();
    bool isOTAActive() const { return _otaActive; }

private:
    bool _otaActive;
    unsigned long _otaStartMs;
};

extern TxOtaManager txOTA;

#endif // TX_OTA_MANAGER_H
