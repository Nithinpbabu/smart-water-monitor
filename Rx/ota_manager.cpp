#include "ota_manager.h"
#include <ArduinoOTA.h>
#include "espnow_gateway.h"
#include "mqtt_client.h"

RxOTAManager rxOTA;

RxOTAManager::RxOTAManager() : _isOTAActive(false), _otaStartTimeMs(0) {}

bool RxOTAManager::begin() {
    ArduinoOTA.setHostname("WaterTank-Rx-Gateway");
    
    ArduinoOTA.onStart([]() {
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH)
            type = "sketch";
        else // U_SPIFFS
            type = "filesystem";
        Serial.println("[OTA Rx] Start updating " + type);
    });
    
    ArduinoOTA.onEnd([]() {
        Serial.println("\n[OTA Rx] Update Complete. Rebooting...");
    });
    
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("[OTA Rx] Progress: %u%%\r", (progress / (total / 100)));
    });
    
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("[OTA Rx] Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
        else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });
    
    ArduinoOTA.begin();
    Serial.println("[OTA Rx] ArduinoOTA Engine initialized.");
    return true;
}

void RxOTAManager::startOTAMode() {
    _isOTAActive = true;
    _otaStartTimeMs = millis();
    Serial.println("[OTA Rx] Wireless OTA Mode ACTIVATED! (10-minute safety timeout active).");
    espNowGateway.queueSessionCommand(MSG_OTA_START);
    rxMqtt.publishOtaCommand("tx01", "OTA_START");
}

void RxOTAManager::endOTAMode() {
    _isOTAActive = false;
    Serial.println("[OTA Rx] Exiting Wireless OTA Mode. Rebooting Gateway...");
    espNowGateway.queueSessionCommand(MSG_OTA_END);
    rxMqtt.publishOtaCommand("tx01", "OTA_EXIT");
    delay(500);
    ESP.restart();
}

void RxOTAManager::update() {
    if (_isOTAActive) {
        ArduinoOTA.handle();
        
        // 10-minute safety timeout check
        if (millis() - _otaStartTimeMs >= 600000) {
            Serial.println("[OTA Rx] Safety Timeout (10 minutes) reached without flashing. Exiting OTA mode...");
            endOTAMode();
        }
    }
}
