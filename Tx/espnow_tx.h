#ifndef TX_ESPNOW_TX_H
#define TX_ESPNOW_TX_H

#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_arduino_version.h>
#include "protocol.h"

struct InboundRxMessage {
    bool     hasMessage;
    uint8_t  messageType;
    uint32_t configVersion;
    uint8_t  rxMotorStatus;
    ConfigPacket configData;
};

class EspNowTxManager {
public:
    EspNowTxManager();
    bool begin();
    
    void setTargetMac(const uint8_t* mac);
    bool sendSensorData(const SensorPacket& packet);
    bool sendFullDetected(const FullDetectedPacket& packet);
    bool sendConfigAck(uint32_t version, uint8_t msgType = MSG_CONFIG_ACK);
    
    InboundRxMessage getLastInboundMessage() const { return _lastInbound; }
    void clearInboundMessage() { _lastInbound.hasMessage = false; }
    
    bool waitForAck(uint32_t timeoutMs);

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    static void onDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status);
    static void onDataRecv(const esp_now_recv_info *info, const uint8_t *data, int len);
#else
    static void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status);
    static void onDataRecv(const uint8_t *mac_addr, const uint8_t *data, int len);
#endif

private:
    uint8_t _targetMac[6];
    static InboundRxMessage _lastInbound;
    static volatile bool sendComplete;
    static volatile bool sendSuccess;
    
    void registerPeer(uint8_t channel = 0);
};

extern EspNowTxManager espNowTx;

#endif // TX_ESPNOW_TX_H
