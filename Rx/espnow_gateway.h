#ifndef RX_ESPNOW_GATEWAY_H
#define RX_ESPNOW_GATEWAY_H

#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <esp_arduino_version.h>
#include "protocol.h"

class EspNowGatewayManager {
public:
    EspNowGatewayManager();
    bool begin();
    
    void registerPeer(const uint8_t* mac);
    bool sendAck(const uint8_t* targetMac, uint32_t sequence);
    bool sendMotorCommand(uint8_t msgType);
    bool sendConfigPacket(uint8_t msgType, const ConfigPacket& cfg, const uint8_t* targetMac = NULL);
    bool sendSessionCommand(uint8_t msgType, const uint8_t* targetMac = NULL);
    void queueSessionCommand(uint8_t cmd);
    
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    static void onDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status);
    static void onDataRecv(const esp_now_recv_info *info, const uint8_t *data, int len);
#else
    static void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status);
    static void onDataRecv(const uint8_t *mac_addr, const uint8_t *data, int len);
#endif

    ConfigPacket getActiveConfig() const { return _activeConfig; }
    void updateActiveConfig(const ConfigPacket& cfg);
    
    // Pending State Engine methods
    void requestSessionStart();
    void requestSessionEnd();
    void markConfigPending();
    bool isSessionActive() const { return _isSessionActive; }

private:
    uint8_t _lastTxMac[6];
    bool _txMacKnown;
    ConfigPacket _activeConfig;
    
    // Unified Pending State Flags
    bool _isSessionActive;
    uint8_t _pendingSessionCommand; // 0, MSG_CONFIG_SESSION_START, MSG_CONFIG_SESSION_END, or MSG_OTA_START
    bool _pendingConfigPush;
};

extern EspNowGatewayManager espNowGateway;

#endif // RX_ESPNOW_GATEWAY_H
