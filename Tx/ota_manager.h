#ifndef TX_OTA_MANAGER_H
#define TX_OTA_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>

class TxOtaManager {
public:
    TxOtaManager();
    bool begin(const char* ssid, const char* pass);
    void update();
    void stop();
    
    void startOTAMode();
    void clearOTAMode();
    bool isOTAActive() const { return _isOTAActive; }

private:
    bool _isOTAActive;
    uint32_t _otaStartTimeMs;
    
    WiFiClient _wifiClient;
    PubSubClient _mqttClient;
    
    void setupMqtt();
    void handleMqtt();
    static void mqttCallback(char* topic, byte* payload, unsigned int length);
};

typedef TxOtaManager TxOTAManager;

extern TxOtaManager txOTA;

#endif // TX_OTA_MANAGER_H
