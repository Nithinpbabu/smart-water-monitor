#include "espnow_gateway.h"
#include "motor_controller.h"
#include "mqtt_client.h"
#include "pre_peak_fill.h"

EspNowGatewayManager espNowGateway;

static uint8_t broadcastAddress[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
void EspNowGatewayManager::onDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
#else
void EspNowGatewayManager::onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
#endif
    if (status != ESP_NOW_SEND_SUCCESS) {
        Serial.printf("[ESP-NOW Rx] Command Send Status: FAILED (%d)\n", status);
    }
}

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
void EspNowGatewayManager::onDataRecv(const esp_now_recv_info *info, const uint8_t *data, int len) {
    if (len < 1) return;
    const uint8_t* srcMac = info->src_addr;
#else
void EspNowGatewayManager::onDataRecv(const uint8_t *mac_addr, const uint8_t *data, int len) {
    if (len < 1) return;
    const uint8_t* srcMac = mac_addr;
#endif
    
    uint8_t msgType = data[0];
    
    if (msgType == MSG_SENSOR_DATA && len >= sizeof(SensorPacket)) {
        SensorPacket pkt;
        memcpy(&pkt, data, sizeof(SensorPacket));
        
        // 1. Auto-register Tx MAC as unicast peer
        espNowGateway.registerPeer(srcMac);
        
        // Update telemetry watchdog timestamp
        motorCtrl.updateTelemetryTimestamp();
        
        // 2. Evaluate auto-fill triggers FIRST so motor state is updated before building ACK/Response
        ConfigPacket currentCfg = espNowGateway.getActiveConfig();
        if (!motorCtrl.isMotorOn() && pkt.waterPercentage <= currentCfg.lowWaterThreshold && currentCfg.lowWaterThreshold > 0) {
            Serial.printf("[Rx Gateway] AUTO-FILL TRIGGERED! Water percentage %u%% <= Threshold %u%%\n",
                          pkt.waterPercentage, currentCfg.lowWaterThreshold);
            motorCtrl.turnOn(currentCfg.maxMotorRuntimeSeconds, "AUTO_LOW_WATER_TRIGGER");
            rxMqtt.publishMotorEvent("AUTO_START", "ON", "LOW_WATER_THRESHOLD");
        }
        
        // Evaluate Pre-Peak Fill Trigger
        prePeakFill.checkAndTrigger(pkt.waterPercentage, currentCfg.maxMotorRuntimeSeconds);
        
        // Sync Rx active config version to match Tx if Tx is on a higher version
        if (pkt.configVersion > currentCfg.configVersion) {
            currentCfg.configVersion = pkt.configVersion;
            espNowGateway.updateActiveConfig(currentCfg);
        }

        // 3. UNIFIED PENDING STATE ENGINE: Evaluate queued commands for Tx wake-up
        if (espNowGateway._pendingSessionCommand != 0) {
            uint8_t sessionCmd = espNowGateway._pendingSessionCommand;
            espNowGateway._pendingSessionCommand = 0; // Clear pending command
            Serial.printf("[Rx Gateway] Delivering queued Session Command 0x%02X to Tx on wake-up...\n", sessionCmd);
            espNowGateway.sendSessionCommand(sessionCmd, srcMac);
        }
        else if (currentCfg.configVersion > pkt.configVersion || espNowGateway._pendingConfigPush) {
            if (espNowGateway._pendingConfigPush && currentCfg.configVersion <= pkt.configVersion) {
                currentCfg.configVersion = pkt.configVersion + 1;
                espNowGateway.updateActiveConfig(currentCfg);
            }
            uint8_t cfgMsgType = MSG_CONFIG_SAVE;
            espNowGateway._pendingConfigPush = false; // Clear pending push flag
            Serial.printf("[Rx Gateway] Delivering ConfigPacket 0x%02X (Ver %u) to Tx on wake-up...\n",
                          cfgMsgType, currentCfg.configVersion);
            espNowGateway.sendConfigPacket(cfgMsgType, currentCfg, srcMac);
        }
        else if (prePeakFill.isWarmupActive(currentCfg.normalSleepIntervalSeconds)) {
            ConfigPacket warmupCfg = currentCfg;
            warmupCfg.normalSleepIntervalSeconds = 30; // 30-second fast sleep during Pre-Peak Warmup window
            Serial.printf("[Rx Gateway] PRE-PEAK WARMUP ACTIVE (%u min before peak)! Pushing 30s sleep interval to Tx...\n",
                          prePeakFill.getWarmupMinutes(currentCfg.normalSleepIntervalSeconds));
            espNowGateway.sendConfigPacket(MSG_CONFIG_PREVIEW, warmupCfg, srcMac);
        }
        else {
            // Send standard ACK containing updated motor state
            espNowGateway.sendAck(srcMac, pkt.sequence);
        }
        
        // 4. Log and publish telemetry
        Serial.printf("[ESP-NOW Rx] Received Packet type 0x%02X (%d bytes) from MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                      msgType, len, srcMac[0], srcMac[1], srcMac[2], srcMac[3], srcMac[4], srcMac[5]);
        Serial.printf("[Rx Gateway] TELEMETRY RECV #%u | Dist=%ucm, Water=%u%%, Bat=%.2fV (%u%%), TxMotor=%s, ConfigV=%u\n",
                      pkt.sequence, pkt.waterDistanceCm, pkt.waterPercentage,
                      pkt.batteryVoltageMv / 1000.0f, pkt.batteryPercentage,
                      pkt.motorStatus ? "ON" : "OFF", pkt.configVersion);
                      
        // 5. Publish Telemetry JSON to MQTT
        // Override pkt.motorStatus with Rx Gateway's actual motor relay state so telemetry reflects real-time status!
        pkt.motorStatus = motorCtrl.isMotorOn() ? 1 : 0;
        rxMqtt.publishTelemetry(pkt);
    }
    else if (msgType == MSG_FULL_DETECTED) {
        Serial.println("[Rx Gateway] FULL_DETECTED event received from Tx!");
        motorCtrl.handleFullDetected();
        
        // Send MOTOR_OFF command immediately to Tx
        espNowGateway.sendMotorCommand(MSG_MOTOR_OFF);
        rxMqtt.publishMotorEvent("FULL_DETECTED", "OFF", "TANK_100_FULL");
    }
    else if (msgType == MSG_CONFIG_ACK || msgType == MSG_CONFIG_PREVIEW_ACK || msgType == MSG_CONFIG_SAVE_ACK || msgType == MSG_CONFIG_SESSION_ACK || msgType == MSG_CONFIG_SESSION_END_ACK) {
        Serial.printf("[Rx Gateway] Configuration ACK (Type 0x%02X) confirmed by Tx!\n", msgType);
    }
}

EspNowGatewayManager::EspNowGatewayManager() 
    : _txMacKnown(false), _isSessionActive(false), _pendingSessionCommand(0), _pendingConfigPush(false) {
    _activeConfig.messageType = MSG_CONFIG_RESPONSE;
    _activeConfig.configVersion = 1;
    _activeConfig.waterTopLevelCm = 25;
    _activeConfig.waterBottomLevelCm = 150;
    _activeConfig.normalSleepIntervalSeconds = 300;
    _activeConfig.motorMonitoringIntervalSeconds = 10;
    _activeConfig.lowWaterThreshold = 20;
    _activeConfig.maxMotorRuntimeSeconds = 3600;
}

bool EspNowGatewayManager::begin() {
    Serial.printf("[ESP-NOW Rx] Receiver MAC Address: %s\n", WiFi.macAddress().c_str());
    
    if (esp_now_init() != ESP_OK) {
        Serial.println("[ESP-NOW Rx] Error initializing ESP-NOW!");
        return false;
    }
    
    esp_now_register_send_cb(onDataSent);
    esp_now_register_recv_cb(onDataRecv);
    
    registerPeer(broadcastAddress);
    return true;
}

void EspNowGatewayManager::registerPeer(const uint8_t* mac) {
    if (mac == NULL) return;
    
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, mac, 6);
    peerInfo.channel = 0; // Use current Wi-Fi channel
    peerInfo.encrypt = false;
    
    if (esp_now_is_peer_exist(mac)) {
        return; // Already registered as peer
    }
    
    esp_err_t addRes = esp_now_add_peer(&peerInfo);
    if (addRes == ESP_OK) {
        Serial.printf("[ESP-NOW Rx] Successfully registered Unicast Peer MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    } else {
        Serial.printf("[ESP-NOW Rx] Failed to register peer MAC (Error: %d)\n", addRes);
    }
}

void EspNowGatewayManager::requestSessionStart() {
    _isSessionActive = true;
    _pendingSessionCommand = MSG_CONFIG_SESSION_START;
    Serial.println("[Rx Gateway] Live Session START requested. Queued for Tx wake-up...");
    sendSessionCommand(MSG_CONFIG_SESSION_START);
}

void EspNowGatewayManager::requestSessionEnd() {
    _isSessionActive = false;
    _pendingSessionCommand = MSG_CONFIG_SESSION_END;
    _pendingConfigPush = false;
    Serial.println("[Rx Gateway] Live Session END requested. Queued for Tx wake-up...");
    sendSessionCommand(MSG_CONFIG_SESSION_END);
}

void EspNowGatewayManager::queueSessionCommand(uint8_t cmd) {
    _pendingSessionCommand = cmd;
    Serial.printf("[Rx Gateway] Queued Session Command 0x%02X for Tx wake-up...\n", cmd);
    sendSessionCommand(cmd);
}

void EspNowGatewayManager::markConfigPending() {
    _pendingConfigPush = true;
}

void EspNowGatewayManager::updateActiveConfig(const ConfigPacket& cfg) {
    _activeConfig = cfg;
    _pendingConfigPush = true;
    Serial.printf("[Rx Gateway] Updated active config cache v%u: Top=%ucm, Bot=%ucm, LowTh=%u%%\n",
                  _activeConfig.configVersion, _activeConfig.waterTopLevelCm, 
                  _activeConfig.waterBottomLevelCm, _activeConfig.lowWaterThreshold);
}

bool EspNowGatewayManager::sendAck(const uint8_t* targetMac, uint32_t sequence) {
    if (targetMac != NULL) {
        registerPeer(targetMac);
    }
    
    AckPacket ack;
    ack.messageType = MSG_ACK;
    ack.sequence = sequence;
    ack.configVersion = _activeConfig.configVersion;
    ack.rxMotorStatus = motorCtrl.isMotorOn() ? MOTOR_STATUS_ON : MOTOR_STATUS_OFF;
    
    const uint8_t* destMac = targetMac ? targetMac : broadcastAddress;
    esp_err_t res = esp_now_send(destMac, (const uint8_t*)&ack, sizeof(AckPacket));
    
    if (res != ESP_OK) {
        Serial.printf("[Rx Gateway] Error sending ACK to %02X:%02X:%02X:%02X:%02X:%02X (Code: %d)\n",
                      destMac[0], destMac[1], destMac[2], destMac[3], destMac[4], destMac[5], res);
    }
    return (res == ESP_OK);
}

bool EspNowGatewayManager::sendMotorCommand(uint8_t msgType) {
    MotorCommandPacket cmd;
    cmd.messageType = msgType;
    cmd.sequence = millis();
    cmd.maxMotorRuntimeSeconds = motorCtrl.getMaxRuntimeSeconds();
    
    Serial.printf("[Rx Gateway] Sending Motor Command 0x%02X to Tx...\n", msgType);
    esp_err_t res = esp_now_send(broadcastAddress, (const uint8_t*)&cmd, sizeof(MotorCommandPacket));
    return (res == ESP_OK);
}

bool EspNowGatewayManager::sendConfigPacket(uint8_t msgType, const ConfigPacket& cfg, const uint8_t* targetMac) {
    ConfigPacket pkt = cfg;
    pkt.messageType = msgType;
    
    const uint8_t* destMac = targetMac ? targetMac : broadcastAddress;
    if (targetMac) {
        registerPeer(targetMac);
    }
    
    Serial.printf("[Rx Gateway] Sending ConfigPacket 0x%02X (Ver %u) to Tx...\n", msgType, cfg.configVersion);
    esp_err_t res = esp_now_send(destMac, (const uint8_t*)&pkt, sizeof(ConfigPacket));
    return (res == ESP_OK);
}

bool EspNowGatewayManager::sendSessionCommand(uint8_t msgType, const uint8_t* targetMac) {
    AckPacket cmd;
    cmd.messageType = msgType;
    cmd.sequence = millis();
    cmd.configVersion = _activeConfig.configVersion;
    cmd.rxMotorStatus = motorCtrl.isMotorOn() ? 1 : 0;
    
    const uint8_t* destMac = targetMac ? targetMac : broadcastAddress;
    if (targetMac) {
        registerPeer(targetMac);
    }
    
    Serial.printf("[Rx Gateway] Sending Session Command 0x%02X to Tx...\n", msgType);
    esp_err_t res = esp_now_send(destMac, (const uint8_t*)&cmd, sizeof(AckPacket));
    return (res == ESP_OK);
}
