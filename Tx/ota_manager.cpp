#include "ota_manager.h"
#include "motor_state.h"
#include <esp_now.h>

TxOtaManager txOTA;

TxOtaManager::TxOtaManager() 
    : _isOTAActive(false), _otaStartTimeMs(0), _mqttClient(_wifiClient) {}

void TxOtaManager::mqttCallback(char* topic, byte* payload, unsigned int length) {
    char message[64];
    if (length >= sizeof(message)) length = sizeof(message) - 1;
    memcpy(message, payload, length);
    message[length] = '\0';
    
    Serial.printf("[Tx OTA MQTT] Received command on '%s': %s\n", topic, message);
    
    String msgStr = String(message);
    if (msgStr == "OTA_EXIT" || msgStr.indexOf("OTA_EXIT") >= 0) {
        Serial.println("[Tx OTA MQTT] Received OTA_EXIT command over MQTT! Sending ACK & Rebooting...");
        
        // Send ACK over MQTT before rebooting
        txOTA._mqttClient.publish("tank/tx01/ota/ack", "{\"status\":\"OTA_EXITING\",\"node\":\"tx01\"}");
        delay(200);
        
        txOTA.stop();
    }
}

void TxOtaManager::setupMqtt() {
    _mqttClient.setServer("192.168.29.211", 1883);
    _mqttClient.setCallback(mqttCallback);
    
    String clientId = "WaterTankTxOTA-" + String(random(0xffff), HEX);
    Serial.println("[Tx OTA MQTT] Connecting to MQTT broker at 192.168.29.211:1883...");
    
    if (_mqttClient.connect(clientId.c_str())) {
        Serial.println("[Tx OTA MQTT] Connected to MQTT broker successfully!");
        _mqttClient.subscribe("tank/tx01/ota/cmd");
        _mqttClient.publish("tank/tx01/ota/ack", "{\"status\":\"OTA_READY\",\"node\":\"tx01\"}");
    } else {
        Serial.printf("[Tx OTA MQTT] Connection failed, state=%d (Will retry in loop)\n", _mqttClient.state());
    }
}

void TxOtaManager::handleMqtt() {
    if (!_mqttClient.connected()) {
        static uint32_t lastRetry = 0;
        if (millis() - lastRetry > 5000) {
            lastRetry = millis();
            String clientId = "WaterTankTxOTA-" + String(random(0xffff), HEX);
            if (_mqttClient.connect(clientId.c_str())) {
                Serial.println("[Tx OTA MQTT] Reconnected to MQTT broker!");
                _mqttClient.subscribe("tank/tx01/ota/cmd");
            }
        }
    } else {
        _mqttClient.loop();
    }
}

bool TxOtaManager::begin(const char* ssid, const char* password) {
    Serial.println("\n[Tx OTA] Initiating Wireless OTA Mode...");
    
    // 1. De-initialize ESP-NOW and cleanly reset Wi-Fi radio & LwIP socket stack
    esp_now_deinit();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(100);
    
    Serial.printf("[Tx OTA] Connecting to Wi-Fi SSID '%s'...\n", ssid);
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    
    // 2. Disable Wi-Fi Modem Sleep so radio stays 100% active continuously
    WiFi.setSleep(false);
    
    uint32_t startMs = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - startMs < 15000)) {
        delay(500);
        Serial.print(".");
    }
    Serial.println();
    
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[Tx OTA] Wi-Fi connection failed! Cannot enter OTA mode.");
        return false;
    }
    
    Serial.printf("[Tx OTA] Wi-Fi connected! IP Address: %s\n", WiFi.localIP().toString().c_str());
    
    // 3. Configure ArduinoOTA parameters & explicit UDP/TCP Port 3232
    ArduinoOTA.setPort(3232);
    ArduinoOTA.setHostname("water-tank-tx");
    
    ArduinoOTA.onStart([]() {
        String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
        Serial.println("[Tx OTA] Flash Update Started: " + type);
    });
    
    ArduinoOTA.onEnd([]() {
        Serial.println("\n[Tx OTA] Flash Update Complete! Clearing RTC OTA Mode & Rebooting...");
        txState.clearOTAMode();
    });
    
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("[Tx OTA] Progress: %u%%\r", (progress / (total / 100)));
    });
    
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("[Tx OTA] Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
        else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });
    
    ArduinoOTA.begin();
    
    // 4. Initialize MQTT connection for OTA exit command listener
    setupMqtt();
    
    _isOTAActive = true;
    _otaStartTimeMs = millis();
    
    Serial.println("[Tx OTA] ArduinoOTA Ready! Node active on Network Port 'water-tank-tx.local' (Port 3232)");
    Serial.println("[Tx OTA] MQTT OTA Command Listener Active on 'tank/tx01/ota/cmd'.");
    Serial.println("[Tx OTA] 10-minute safety inactivity timer running.");
    return true;
}

void TxOtaManager::update() {
    if (!_isOTAActive) return;
    
    ArduinoOTA.handle();
    handleMqtt();
    
    // Safety 10-Minute Timeout Check (600,000 ms)
    if (millis() - _otaStartTimeMs >= 600000) {
        Serial.println("[Tx OTA] 10-minute safety inactivity timeout reached! Auto-rebooting to Normal Mode...");
        txState.clearOTAMode();
        delay(500);
        ESP.restart();
    }
}

void TxOtaManager::stop() {
    Serial.println("[Tx OTA] Exit requested. Rebooting node to Normal Mode...");
    txState.clearOTAMode();
    delay(500);
    ESP.restart();
}
