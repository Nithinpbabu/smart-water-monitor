#include <Arduino.h>
#include "protocol.h"
#include "antenna_select.h"
#include "battery.h"
#include "water_sensor.h"
#include "sleep_manager.h"
#include "config.h"
#include "espnow_tx.h"
#include "motor_state.h"
#include "ota_manager.h"
#include "secrets.h"  // gitignored — copy secrets.h.example and fill in credentials

const bool TX_USE_EXTERNAL_ANTENNA = true;
uint8_t TARGET_RX_MAC[6] = { 0x94, 0xA9, 0x90, 0x03, 0x4B, 0xB8 };

void handleInboundMessage();
void performMeasurementAndTransmit();

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n==============================================");
    Serial.println("   WATER TANK MONITOR V2 - TX NODE (ESP32-C3)");
    Serial.println("==============================================");
    
    esp_reset_reason_t resetReason = esp_reset_reason();
    Serial.printf("[Tx Main] ESP32 Reset Reason: %d ", resetReason);
    switch (resetReason) {
        case ESP_RST_POWERON:   Serial.println("(POWER-ON)"); break;
        case ESP_RST_SW:        Serial.println("(SOFTWARE RESET)"); break;
        case ESP_RST_PANIC:     Serial.println("(EXCEPTION/PANIC)"); break;
        case ESP_RST_INT_WDT:   Serial.println("(INTERRUPT WATCHDOG)"); break;
        case ESP_RST_TASK_WDT:  Serial.println("(TASK WATCHDOG)"); break;
        case ESP_RST_DEEPSLEEP: Serial.println("(DEEP SLEEP WAKE)"); break;
        case ESP_RST_BROWNOUT:  Serial.println("(BROWNOUT RESET)"); break;
        default:                Serial.println("(OTHER)"); break;
    }
    
    configureTxAntenna(TX_USE_EXTERNAL_ANTENNA ? ANTENNA_EXTERNAL : ANTENNA_INTERNAL);
    
    sleepMgr.begin();
    configMgr.begin();
    batteryMon.begin();
    waterSensor.begin();
    
    if (txState.getCurrentMode() != MODE_OTA) {
        espNowTx.setTargetMac(TARGET_RX_MAC);
        if (!espNowTx.begin()) {
            Serial.println("[Tx Main] ESP-NOW initialization failed!");
        }
        performMeasurementAndTransmit();
    } else {
        Serial.println("[Tx Main] Resuming RTC-Persisted OTA Mode after reset!");
    }
}

void loop() {
    TxOperatingMode mode = txState.getCurrentMode();
    TankConfig cfg = configMgr.getEffectiveConfig();
    
    if (mode == MODE_NORMAL) {
        if (txState.shouldShortSleep()) {
            Serial.printf("[Tx Main] LOW WATER & ACK FAILED! Short-sleeping 15s to re-sync (Attempt %u/3)...\n",
                          txState.getShortSleepRetries());
            sleepMgr.goToDeepSleep(15);
        } else {
            Serial.printf("[Tx Main] NORMAL mode active. Going to deep sleep for %u seconds.\n", 
                          cfg.normalSleepIntervalSeconds);
            sleepMgr.goToDeepSleep(cfg.normalSleepIntervalSeconds);
        }
    } 
    else if (mode == MODE_MOTOR_FILLING) {
        uint32_t intervalMs = cfg.motorMonitoringIntervalSeconds * 1000;
        if (intervalMs == 0) intervalMs = 10000;
        
        Serial.printf("[Tx Main] MOTOR_FILLING mode active (NO deep sleep). Waiting %us for next report...\n", 
                      cfg.motorMonitoringIntervalSeconds);
        delay(intervalMs);
        performMeasurementAndTransmit();
    } 
    else if (mode == MODE_CONFIG_SESSION) {
        if (txState.checkSessionTimeout()) {
            return;
        }
        Serial.println("[Tx Main] CONFIG_SESSION active (NO deep sleep). Live preview reporting...");
        delay(2000);
        performMeasurementAndTransmit();
    }
    else if (mode == MODE_OTA) {
        if (!txOTA.isOTAActive()) {
            Serial.println("[Tx Main] Starting top-level OTA Wi-Fi initialization...");
            txOTA.begin(OTA_WIFI_SSID, OTA_WIFI_PASS);
        }
        txOTA.update();
        delay(10);
    }
}

void performMeasurementAndTransmit() {
    TankConfig cfg = configMgr.getEffectiveConfig();
    
    WaterReading water = waterSensor.readSensor(cfg);
    BatteryReading battery = batteryMon.readBattery();
    
    bool isFull = txState.updateState(water.percentage);
    
    SensorPacket packet;
    packet.messageType = MSG_SENSOR_DATA;
    packet.sequence = sleepMgr.getBootCount();
    packet.configVersion = cfg.configVersion;
    packet.waterDistanceCm = water.distanceCm;
    packet.waterPercentage = water.percentage;
    packet.batteryVoltageMv = battery.voltageMv;
    packet.batteryPercentage = battery.percentage;
    packet.sensorStatus = water.status;
    packet.motorStatus = txState.getMotorStatus();
    
    Serial.printf("[Tx Main] Transmitting Telemetry #%u | Dist=%ucm, Water=%u%%, Bat=%.2fV (%u%%), Motor=%s, ConfigV=%u\n",
                  packet.sequence, packet.waterDistanceCm, packet.waterPercentage,
                  packet.batteryVoltageMv / 1000.0f, packet.batteryPercentage,
                  packet.motorStatus ? "ON" : "OFF", packet.configVersion);
                  
    espNowTx.clearInboundMessage();
    bool txSuccess = espNowTx.sendSensorData(packet);
    
    txState.recordTransmissionResult(txSuccess, water.percentage, cfg.lowWaterThreshold);
    
    if (isFull) {
        FullDetectedPacket fullPkt;
        fullPkt.messageType = MSG_FULL_DETECTED;
        fullPkt.sequence = sleepMgr.getBootCount();
        fullPkt.waterDistanceCm = water.distanceCm;
        fullPkt.waterPercentage = 100;
        
        Serial.println("[Tx Main] SENDING FULL_DETECTED EVENT TO RX!");
        espNowTx.sendFullDetected(fullPkt);
    }
    
    if (txSuccess) {
        handleInboundMessage();
    } else {
        Serial.println("[Tx Main] Telemetry transmission failed after 10 retries.");
    }
}

void handleInboundMessage() {
    InboundRxMessage in = espNowTx.getLastInboundMessage();
    if (!in.hasMessage) return;
    
    txState.touchSessionActivity();
    
    Serial.printf("[Tx Main] Processing Inbound Message 0x%02X from RX\n", in.messageType);
    
    if (in.messageType == MSG_ACK) {
        txState.setMotorStatus(in.rxMotorStatus);
        
        if (in.configVersion > configMgr.getActiveConfig().configVersion) {
            Serial.printf("[Tx Main] RX reported newer Config Version (%u vs %u)\n",
                          in.configVersion, configMgr.getActiveConfig().configVersion);
        }
    }
    else if (in.messageType == MSG_MOTOR_ON) {
        txState.setMotorStatus(MOTOR_STATUS_ON);
    }
    else if (in.messageType == MSG_MOTOR_OFF) {
        txState.setMotorStatus(MOTOR_STATUS_OFF);
    }
    else if (in.messageType == MSG_CONFIG_SESSION_START) {
        txState.startConfigSession();
        espNowTx.sendConfigAck(configMgr.getActiveConfig().configVersion, MSG_CONFIG_SESSION_ACK);
    }
    else if (in.messageType == MSG_CONFIG_PREVIEW) {
        TankConfig previewCfg;
        previewCfg.waterTopLevelCm = in.configData.waterTopLevelCm;
        previewCfg.waterBottomLevelCm = in.configData.waterBottomLevelCm;
        previewCfg.normalSleepIntervalSeconds = in.configData.normalSleepIntervalSeconds;
        previewCfg.motorMonitoringIntervalSeconds = in.configData.motorMonitoringIntervalSeconds;
        previewCfg.lowWaterThreshold = in.configData.lowWaterThreshold;
        previewCfg.maxMotorRuntimeSeconds = in.configData.maxMotorRuntimeSeconds;
        previewCfg.configVersion = configMgr.getActiveConfig().configVersion;
        
        configMgr.applyPreviewConfig(previewCfg);
        espNowTx.sendConfigAck(configMgr.getActiveConfig().configVersion, MSG_CONFIG_PREVIEW_ACK);
    }
    else if (in.messageType == MSG_CONFIG_SAVE) {
        TankConfig saveCfg;
        saveCfg.waterTopLevelCm = in.configData.waterTopLevelCm;
        saveCfg.waterBottomLevelCm = in.configData.waterBottomLevelCm;
        saveCfg.normalSleepIntervalSeconds = in.configData.normalSleepIntervalSeconds;
        saveCfg.motorMonitoringIntervalSeconds = in.configData.motorMonitoringIntervalSeconds;
        saveCfg.lowWaterThreshold = in.configData.lowWaterThreshold;
        saveCfg.maxMotorRuntimeSeconds = in.configData.maxMotorRuntimeSeconds;
        saveCfg.configVersion = in.configData.configVersion;
        
        configMgr.saveConfig(saveCfg);
        espNowTx.sendConfigAck(configMgr.getActiveConfig().configVersion, MSG_CONFIG_SAVE_ACK);
    }
    else if (in.messageType == MSG_CONFIG_SESSION_END) {
        configMgr.clearPreviewConfig();
        txState.endConfigSession();
        espNowTx.sendConfigAck(configMgr.getActiveConfig().configVersion, MSG_CONFIG_SESSION_END_ACK);
    }
    else if (in.messageType == MSG_OTA_START) {
        txState.startOTAMode();
    }
    else if (in.messageType == MSG_OTA_END) {
        txState.clearOTAMode();
        ESP.restart();
    }
}
