#include <Arduino.h>
#include "protocol.h"
#include "motor_controller.h"
#include "wifi_manager.h"
#include "mqtt_client.h"
#include "espnow_gateway.h"
#include "serial_cli.h"
#include "ota_manager.h"
#include "pre_peak_fill.h"

// ============================================================================
// RX GATEWAY HARDWARE & NETWORK CONFIGURATION ATTRIBUTES
// ============================================================================

// 1. Wi-Fi Router Credentials (from water_tank_monitor_v2_architecture.md)
const char* WIFI_SSID = "pallachi4G";
const char* WIFI_PASS = "pallachiyil@123";

// 2. Raspberry Pi 5 MQTT Broker Connection
const char* MQTT_BROKER_IP = "192.168.29.211"; // Raspberry Pi IP Address
const uint16_t MQTT_BROKER_PORT = 1883;         // Standard MQTT Port

// 3. Hardware Pin Assignments (Defined in motor_controller.h)
// - MOTOR_RELAY : GPIO 17
// - STATUS_LED  : GPIO 18

// 4. Target Tx Node MAC Address (Default: Broadcast {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF})
uint8_t TARGET_TX_MAC[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

// Track latest water percentage for periodic loop checks
static uint8_t lastKnownWaterPct = 50; 
static unsigned long lastPrePeakCheck = 0;

// ============================================================================

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n=======================================================");
    Serial.println("   WATER TANK MONITOR V2 - RX GATEWAY (ESP32-S3)");
    Serial.println("=======================================================");
    
    // Initialize Motor Relay (GPIO 17) & Status LED (GPIO 18)
    motorCtrl.begin();
    
    // Initialize Wi-Fi connection with Wi-Fi credentials
    rxWiFi.begin(WIFI_SSID, WIFI_PASS);
    
    // Initialize ESP-NOW Gateway Radio
    if (!espNowGateway.begin()) {
        Serial.println("[Rx Main] ESP-NOW Gateway initialization failed!");
    }
    
    // Initialize Pre-Peak Fill Manager (loads NVS config & completed date)
    prePeakFill.begin();
    
    // Initialize MQTT Client targeting Raspberry Pi 5 Broker
    rxMqtt.begin(MQTT_BROKER_IP, MQTT_BROKER_PORT);
    
    // Initialize ArduinoOTA Engine
    rxOTA.begin();
    
    // Initialize Interactive Serial Monitor CLI
    cli.begin();
}

void loop() {
    // 1. Motor Safety Timer & Timeout Manager
    motorCtrl.update();
    
    // 2. Wi-Fi Connection Health Manager
    rxWiFi.update();
    
    // 3. Pre-Peak Fill Periodic Loop Check (every 5 seconds)
    unsigned long now = millis();
    if (now - lastPrePeakCheck >= 5000) {
        lastPrePeakCheck = now;
        ConfigPacket cfg = espNowGateway.getActiveConfig();
        prePeakFill.checkAndTrigger(lastKnownWaterPct, cfg.maxMotorRuntimeSeconds);
    }
    
    // 4. MQTT Client Loop Manager
    rxMqtt.update();
    
    // 5. ArduinoOTA Handler & Safety Timeout Manager
    rxOTA.update();
    
    // 6. Interactive Serial Monitor CLI Command Processing
    cli.update();
    
    delay(1);
}
