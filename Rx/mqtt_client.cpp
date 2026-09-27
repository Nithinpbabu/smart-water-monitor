#include "mqtt_client.h"
#include <ArduinoJson.h>
#include "espnow_gateway.h"
#include "motor_controller.h"
#include "pre_peak_fill.h"
#include "ota_manager.h"

RxMqttClient rxMqtt;

RxMqttClient::RxMqttClient() : _client(_espClient) {}

void RxMqttClient::begin(const char* server, uint16_t port) {
    _brokerIp = server;
    _brokerPort = port;
    _client.setServer(_brokerIp, _brokerPort);
    _client.setCallback(RxMqttClient::callback);
}

void RxMqttClient::reconnect() {
    if (_client.connected()) return;
    
    Serial.printf("[MQTT] Attempting connection to %s:%u...\n", _brokerIp, _brokerPort);
    String clientId = "WaterTank-Rx-" + String(random(0xffff), HEX);
    
    if (_client.connect(clientId.c_str())) {
        Serial.println("[MQTT] Connected successfully!");
        
        // Subscribe to control topics
        _client.subscribe("tank/tx01/command");
        _client.subscribe("tank/tx01/config/set");
        _client.subscribe("tank/tx01/prepeak/set");
        _client.subscribe("tank/tx01/ota/cmd");
        
        publishLog("rx01", "Rx Gateway MQTT Client Connected");
    } else {
        Serial.printf("[MQTT] Failed, rc=%d. Will retry on next loop...\n", _client.state());
    }
}

void RxMqttClient::callback(char* topic, byte* payload, unsigned int length) {
    String message;
    for (unsigned int i = 0; i < length; i++) {
        message += (char)payload[i];
    }
    
    Serial.printf("[MQTT] Received message on topic '%s': %s\n", topic, message.c_str());
    String topicStr = String(topic);
    
    if (topicStr == "tank/tx01/command") {
        StaticJsonDocument<256> doc;
        DeserializationError err = deserializeJson(doc, message);
        if (!err) {
            const char* cmd = doc["command"];
            if (cmd) {
                if (strcmp(cmd, "MOTOR_ON") == 0) {
                    motorCtrl.turnOn(espNowGateway.getActiveConfig().maxMotorRuntimeSeconds, "MQTT_DASHBOARD_COMMAND");
                    espNowGateway.sendMotorCommand(MSG_MOTOR_ON);
                    rxMqtt.publishMotorEvent("MANUAL_START", "ON", "DASHBOARD_COMMAND");
                }
                else if (strcmp(cmd, "MOTOR_OFF") == 0) {
                    motorCtrl.turnOff("MQTT_DASHBOARD_COMMAND");
                    espNowGateway.sendMotorCommand(MSG_MOTOR_OFF);
                    rxMqtt.publishMotorEvent("MANUAL_STOP", "OFF", "DASHBOARD_COMMAND");
                }
            }
        }
    }
    else if (topicStr == "tank/tx01/config/set") {
        StaticJsonDocument<384> doc;
        DeserializationError err = deserializeJson(doc, message);
        if (!err) {
            ConfigPacket cfg = espNowGateway.getActiveConfig();
            cfg.configVersion++; // Increment version number so Rx > Tx for permanent NVS save
            if (doc.containsKey("water_top_level")) cfg.waterTopLevelCm = doc["water_top_level"];
            if (doc.containsKey("water_bottom_level")) cfg.waterBottomLevelCm = doc["water_bottom_level"];
            if (doc.containsKey("normal_interval")) cfg.normalSleepIntervalSeconds = doc["normal_interval"];
            if (doc.containsKey("motor_monitor_interval")) cfg.motorMonitoringIntervalSeconds = doc["motor_monitor_interval"];
            if (doc.containsKey("low_water_threshold")) cfg.lowWaterThreshold = doc["low_water_threshold"];
            if (doc.containsKey("max_motor_runtime")) cfg.maxMotorRuntimeSeconds = doc["max_motor_runtime"];

            Serial.printf("[MQTT] Updated Active Config v%u from Dashboard: Top=%u cm, Bottom=%u cm, Norm=%u s, Low=%u%%, MaxRun=%u s\n",
                cfg.configVersion, cfg.waterTopLevelCm, cfg.waterBottomLevelCm, cfg.normalSleepIntervalSeconds, cfg.lowWaterThreshold, cfg.maxMotorRuntimeSeconds);

            espNowGateway.updateActiveConfig(cfg);
            espNowGateway.sendConfigPacket(MSG_CONFIG_SAVE, cfg);
            rxMqtt.publishConfigState(cfg);
        }
    }
    else if (topicStr == "tank/tx01/prepeak/set") {
        StaticJsonDocument<256> doc;
        DeserializationError err = deserializeJson(doc, message);
        if (!err) {
            if (doc.containsKey("enabled")) prePeakFill.setEnabled(doc["enabled"]);
            if (doc.containsKey("peak_hour") && doc.containsKey("peak_minute")) {
                prePeakFill.setPeakTime(doc["peak_hour"], doc["peak_minute"]);
            }
            if (doc.containsKey("window_minutes")) prePeakFill.setWindowMinutes(doc["window_minutes"]);
            if (doc.containsKey("water_threshold")) prePeakFill.setWaterThreshold(doc["water_threshold"]);
            
            Serial.println("[MQTT] Updated Pre-Peak Fill Configuration.");
        }
    }
    else if (topicStr == "tank/tx01/ota/cmd") {
        StaticJsonDocument<256> doc;
        DeserializationError err = deserializeJson(doc, message);
        if (!err) {
            const char* cmd = doc["command"];
            if (cmd) {
                if (strcmp(cmd, "OTA_START") == 0) {
                    rxOTA.startOTAMode();
                } else if (strcmp(cmd, "OTA_EXIT") == 0) {
                    rxOTA.endOTAMode();
                }
            }
        }
    }
}

void RxMqttClient::update() {
    if (!_client.connected()) {
        static uint32_t lastReconnectTry = 0;
        if (millis() - lastReconnectTry > 5000) {
            lastReconnectTry = millis();
            reconnect();
        }
    } else {
        _client.loop();
    }
}

void RxMqttClient::publishTelemetry(const SensorPacket& pkt) {
    if (!_client.connected()) return;
    
    StaticJsonDocument<256> doc;
    doc["sequence"] = pkt.sequence;
    doc["water_distance_cm"] = pkt.waterDistanceCm;
    doc["water_percentage"] = pkt.waterPercentage;
    doc["battery_voltage_mv"] = pkt.batteryVoltageMv;
    doc["battery_percentage"] = pkt.batteryPercentage;
    doc["sensor_status"] = pkt.sensorStatus;
    doc["motor_status"] = pkt.motorStatus ? "ON" : "OFF";
    doc["config_version"] = pkt.configVersion;
    
    char buffer[256];
    serializeJson(doc, buffer);
    _client.publish("tank/tx01/telemetry", buffer);
}

void RxMqttClient::publishConfigState(const ConfigPacket& cfg) {
    if (!_client.connected()) return;
    
    StaticJsonDocument<256> doc;
    doc["config_version"] = cfg.configVersion;
    doc["water_top_level"] = cfg.waterTopLevelCm;
    doc["water_bottom_level"] = cfg.waterBottomLevelCm;
    doc["normal_interval"] = cfg.normalSleepIntervalSeconds;
    doc["motor_monitor_interval"] = cfg.motorMonitoringIntervalSeconds;
    doc["low_water_threshold"] = cfg.lowWaterThreshold;
    doc["max_motor_runtime"] = cfg.maxMotorRuntimeSeconds;
    
    char buffer[256];
    serializeJson(doc, buffer);
    _client.publish("tank/tx01/config/state", buffer);
}

void RxMqttClient::publishMotorEvent(const char* event, const char* status, const char* reason) {
    if (!_client.connected()) return;
    
    StaticJsonDocument<192> doc;
    doc["event"] = event;
    doc["motor_status"] = status;
    doc["reason"] = reason;
    
    char buffer[192];
    serializeJson(doc, buffer);
    _client.publish("tank/tx01/motor", buffer);
}

void RxMqttClient::publishLog(const char* node, const char* message) {
    if (!_client.connected()) return;
    
    StaticJsonDocument<256> doc;
    doc["node"] = node;
    doc["message"] = message;
    
    char buffer[256];
    serializeJson(doc, buffer);
    _client.publish("tank/logs", buffer);
}

void RxMqttClient::publishOtaCommand(const char* node, const char* command) {
    if (!_client.connected()) return;
    
    StaticJsonDocument<128> doc;
    doc["node"] = node;
    doc["command"] = command;
    
    char buffer[128];
    serializeJson(doc, buffer);
    _client.publish("tank/tx01/ota/state", buffer);
}
