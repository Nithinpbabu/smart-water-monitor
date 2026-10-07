#include "espnow_tx.h"

EspNowTxManager espNowTx;
InboundRxMessage EspNowTxManager::_lastInbound = { false, 0, 0, 0, {} };
volatile bool EspNowTxManager::sendComplete = false;
volatile bool EspNowTxManager::sendSuccess = false;

// Persist last known working Wi-Fi channel in RTC memory across deep sleep cycles
RTC_DATA_ATTR static uint8_t rtcLastKnownChannel = 1;

EspNowTxManager::EspNowTxManager() {
    memset(_targetMac, 0xFF, 6);
}

void EspNowTxManager::setTargetMac(const uint8_t* mac) {
    if (mac != NULL) {
        memcpy(_targetMac, mac, 6);
    }
}

bool EspNowTxManager::begin() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    
    if (esp_now_init() != ESP_OK) {
        Serial.println("[ESP-NOW Tx] Error initializing ESP-NOW!");
        return false;
    }
    
    esp_now_register_send_cb(onDataSent);
    esp_now_register_recv_cb(onDataRecv);
    
    registerPeer();
    return true;
}

void EspNowTxManager::registerPeer(uint8_t channel) {
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, _targetMac, 6);
    peerInfo.channel = (channel > 0) ? channel : rtcLastKnownChannel;
    peerInfo.encrypt = false;
    
    if (esp_now_is_peer_exist(_targetMac)) {
        esp_now_del_peer(_targetMac);
    }
    esp_now_add_peer(&peerInfo);
}

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
void EspNowTxManager::onDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
#else
void EspNowTxManager::onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
#endif
    sendComplete = true;
    sendSuccess = (status == ESP_NOW_SEND_SUCCESS);
}

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
void EspNowTxManager::onDataRecv(const esp_now_recv_info *info, const uint8_t *data, int len) {
#else
void EspNowTxManager::onDataRecv(const uint8_t *mac_addr, const uint8_t *data, int len) {
#endif
    if (len < 1) return;
    
    uint8_t msgType = data[0];
    _lastInbound.hasMessage = true;
    _lastInbound.messageType = msgType;
    
    if (msgType == MSG_ACK && len >= sizeof(AckPacket)) {
        AckPacket ack;
        memcpy(&ack, data, sizeof(AckPacket));
        _lastInbound.configVersion = ack.configVersion;
        _lastInbound.rxMotorStatus = ack.rxMotorStatus;
    }
    else if ((msgType == MSG_CONFIG_PREVIEW || msgType == MSG_CONFIG_SAVE) && len >= sizeof(ConfigPacket)) {
        memcpy(&_lastInbound.configData, data, sizeof(ConfigPacket));
        _lastInbound.configVersion = _lastInbound.configData.configVersion;
        Serial.printf("[ESP-NOW Tx] Received ConfigPacket (type 0x%02X): ConfigVer=%u\n",
                      msgType, _lastInbound.configVersion);
    }
}

bool EspNowTxManager::waitForAck(uint32_t timeoutMs) {
    unsigned long start = millis();
    while (millis() - start < timeoutMs) {
        if (sendComplete && _lastInbound.hasMessage) {
            return true;
        }
        delay(2);
    }
    return false;
}

bool EspNowTxManager::sendSensorData(const SensorPacket& packet) {
    for (int attempt = 1; attempt <= 10; attempt++) {
        sendComplete = false;
        sendSuccess = false;
        clearInboundMessage();
        
        esp_wifi_set_channel(rtcLastKnownChannel, WIFI_SECOND_CHAN_NONE);
        registerPeer(rtcLastKnownChannel);
        
        esp_err_t result = esp_now_send(_targetMac, (const uint8_t *)&packet, sizeof(SensorPacket));
        if (result == ESP_OK) {
            if (waitForAck(500)) {
                Serial.printf("[ESP-NOW Tx] ACK SUCCESS on Attempt #%d (Channel %u)!\n", 
                              attempt, rtcLastKnownChannel);
                return true;
            }
        }
        
        Serial.printf("[ESP-NOW Tx] Attempt #%d/10 timed out (No ACK). Sweeping channels 1..13...\n", attempt);
        
        for (uint8_t ch = 1; ch <= 13; ch++) {
            if (ch == rtcLastKnownChannel) continue;
            
            esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
            registerPeer(ch);
            
            sendComplete = false;
            sendSuccess = false;
            clearInboundMessage();
            
            esp_now_send(_targetMac, (const uint8_t *)&packet, sizeof(SensorPacket));
            if (waitForAck(100)) {
                rtcLastKnownChannel = ch;
                Serial.printf("[ESP-NOW Tx] ACK SUCCESS! Synced with Rx on Wi-Fi Channel %u (Attempt #%d)!\n", 
                              ch, attempt);
                return true;
            }
        }
        
        delay(50);
    }
    return false;
}

bool EspNowTxManager::sendFullDetected(const FullDetectedPacket& packet) {
    esp_err_t res = esp_now_send(_targetMac, (const uint8_t *)&packet, sizeof(FullDetectedPacket));
    return (res == ESP_OK);
}

bool EspNowTxManager::sendConfigAck(uint32_t version, uint8_t msgType) {
    AckPacket ack;
    ack.messageType = msgType;
    ack.sequence = 0;
    ack.configVersion = version;
    ack.rxMotorStatus = 0;
    esp_err_t res = esp_now_send(_targetMac, (const uint8_t *)&ack, sizeof(AckPacket));
    return (res == ESP_OK);
}
