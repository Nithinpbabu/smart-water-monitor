#include "serial_cli.h"
#include "espnow_gateway.h"
#include "motor_controller.h"
#include "mqtt_client.h"
#include "ota_manager.h"
#include "pre_peak_fill.h"

SerialCLI cli;

SerialCLI::SerialCLI() {
    _inputBuffer = "";
}

void SerialCLI::begin() {
    Serial.println("\n=====================================");
    Serial.println("   WATER TANK MONITOR V2 - RX CLI    ");
    Serial.println("   Type 'help' for command list      ");
    Serial.println("=====================================\n");
}

void SerialCLI::printHelp() {
    Serial.println("\n----------------- SERIAL CLI COMMANDS -----------------");
    Serial.println("  help              - Print this menu");
    Serial.println("  status            - Display system status & active config");
    Serial.println("  motor on          - Manually turn ON motor relay");
    Serial.println("  motor off         - Manually turn OFF motor relay");
    Serial.println("  sim full          - Simulate full tank cut-off");
    Serial.println("  prepeak status    - Display Pre-Peak Fill window & NVS status");
    Serial.println("  prepeak sim       - Simulate Pre-Peak Fill trigger");
    Serial.println("  prepeak reset     - Reset NVS completed date flag for testing");
    Serial.println("  session start     - Send CONFIG_SESSION_START to Tx");
    Serial.println("  session end       - Send CONFIG_SESSION_END to Tx");
    Serial.println("  ota               - Trigger Wireless OTA Update Mode on Rx & Tx");
    Serial.println("  ota exit          - Exit OTA Mode & reboot nodes to Normal Mode");
    Serial.println("  set top <cm>      - Set Tank Top level cm (e.g. set top 15)");
    Serial.println("  set bottom <cm>   - Set Tank Bottom level cm (e.g. set bottom 120)");
    Serial.println("  set norm <sec>    - Set Normal deep sleep interval seconds");
    Serial.println("  set motor <sec>   - Set Motor monitoring interval seconds");
    Serial.println("  set low <pct>     - Set Low water threshold percentage");
    Serial.println("  set max <sec>     - Set Max motor runtime safety limit seconds");
    Serial.println("  save config       - Save and commit Config to Tx (increments version)");
    Serial.println("-------------------------------------------------------------\n");
}

void SerialCLI::printStatus() {
    ConfigPacket cfg = espNowGateway.getActiveConfig();
    Serial.println("\n================ SYSTEM STATUS ================");
    Serial.printf("  Wi-Fi IP Address:    %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("  Wi-Fi MAC Address:   %s\n", WiFi.macAddress().c_str());
    Serial.printf("  MQTT Connected:      %s\n", rxMqtt.isConnected() ? "YES" : "NO");
    Serial.printf("  Motor Relay Status:  %s\n", motorCtrl.isMotorOn() ? "ON (RUNNING)" : "OFF");
    Serial.printf("  Live Session Active: %s\n", espNowGateway.isSessionActive() ? "YES" : "NO");
    Serial.printf("  OTA Mode Active:     %s\n", rxOTA.isOTAActive() ? "YES" : "NO");
    Serial.println("\n-------------- PRE-PEAK FILL STATUS --------------");
    Serial.printf("  %s\n", prePeakFill.getStatusString().c_str());
    Serial.println("\n-------------- ACTIVE CONFIG CACHE --------------");
    Serial.printf("  Config Version:      %u\n", cfg.configVersion);
    Serial.printf("  Water Top Level:     %u cm\n", cfg.waterTopLevelCm);
    Serial.printf("  Water Bottom Level:  %u cm\n", cfg.waterBottomLevelCm);
    Serial.printf("  Normal Sleep Interval: %u sec\n", cfg.normalSleepIntervalSeconds);
    Serial.printf("  Motor Monitoring:    %u sec\n", cfg.motorMonitoringIntervalSeconds);
    Serial.printf("  Low Water Threshold: %u%%\n", cfg.lowWaterThreshold);
    Serial.println("=====================================\n");
}

void SerialCLI::update() {
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\r') continue;
        if (c == '\n') {
            if (_inputBuffer.length() > 0) {
                processCommand(_inputBuffer);
                _inputBuffer = "";
            }
        } else {
            _inputBuffer += c;
        }
    }
}

void SerialCLI::processCommand(const String& rawCmd) {
    String cmd = rawCmd;
    cmd.trim();
    cmd.toLowerCase();
    
    Serial.printf("> Command: '%s'\n", cmd.c_str());
    
    if (cmd == "help") {
        printHelp();
    }
    else if (cmd == "status") {
        printStatus();
    }
    else if (cmd == "motor on") {
        ConfigPacket cfg = espNowGateway.getActiveConfig();
        motorCtrl.turnOn(cfg.maxMotorRuntimeSeconds, "CLI_MANUAL_START");
        espNowGateway.sendMotorCommand(MSG_MOTOR_ON);
        rxMqtt.publishMotorEvent("MANUAL_START", "ON", "CLI_COMMAND");
    }
    else if (cmd == "motor off") {
        motorCtrl.turnOff("CLI_MANUAL_STOP");
        espNowGateway.sendMotorCommand(MSG_MOTOR_OFF);
        rxMqtt.publishMotorEvent("MANUAL_STOP", "OFF", "CLI_COMMAND");
    }
    else if (cmd == "sim full") {
        Serial.println("[CLI] Simulating FULL_DETECTED cutoff...");
        motorCtrl.handleFullDetected();
        espNowGateway.sendMotorCommand(MSG_MOTOR_OFF);
        rxMqtt.publishMotorEvent("FULL_DETECTED", "OFF", "CLI_SIMULATION");
    }
    else if (cmd == "prepeak" || cmd == "prepeak status") {
        Serial.println("\n---------------- PRE-PEAK FILL STATUS ----------------");
        Serial.printf("%s\n", prePeakFill.getStatusString().c_str());
        Serial.println("------------------------------------------------------\n");
    }
    else if (cmd == "prepeak sim") {
        ConfigPacket cfg = espNowGateway.getActiveConfig();
        prePeakFill.simulateTrigger(45, cfg.maxMotorRuntimeSeconds);
        espNowGateway.sendMotorCommand(MSG_MOTOR_ON);
    }
    else if (cmd == "prepeak reset") {
        prePeakFill.resetCompletedDate();
        Serial.println("[CLI] Pre-Peak Fill NVS completed date flag reset!");
    }
    else if (cmd == "session start") {
        espNowGateway.requestSessionStart();
        Serial.println("[CLI] Live session START queued for Tx.");
    }
    else if (cmd == "session end") {
        espNowGateway.requestSessionEnd();
        Serial.println("[CLI] Live session END queued for Tx.");
    }
    else if (cmd == "ota" || cmd == "ota start") {
        Serial.println("[CLI] Triggering Wireless OTA Update Mode on Rx & Tx...");
        rxOTA.startOTAMode();
    }
    else if (cmd == "ota exit" || cmd == "ota end") {
        Serial.println("[CLI] Exiting OTA Mode & rebooting nodes...");
        rxOTA.endOTAMode();
    }
    else if (cmd.startsWith("set top ")) {
        int val = cmd.substring(8).toInt();
        if (val > 0) {
            ConfigPacket cfg = espNowGateway.getActiveConfig();
            cfg.waterTopLevelCm = (uint16_t)val;
            espNowGateway.updateActiveConfig(cfg);
            espNowGateway.sendConfigPacket(MSG_CONFIG_PREVIEW, cfg);
            Serial.printf("[CLI] Updated Top Level to %d cm & queued preview for Tx.\n", val);
        }
    }
    else if (cmd.startsWith("set bottom ")) {
        int val = cmd.substring(11).toInt();
        if (val > 0) {
            ConfigPacket cfg = espNowGateway.getActiveConfig();
            cfg.waterBottomLevelCm = (uint16_t)val;
            espNowGateway.updateActiveConfig(cfg);
            espNowGateway.sendConfigPacket(MSG_CONFIG_PREVIEW, cfg);
            Serial.printf("[CLI] Updated Bottom Level to %d cm & queued preview for Tx.\n", val);
        }
    }
    else if (cmd.startsWith("set norm ")) {
        int val = cmd.substring(9).toInt();
        if (val >= 5) {
            ConfigPacket cfg = espNowGateway.getActiveConfig();
            cfg.normalSleepIntervalSeconds = (uint32_t)val;
            espNowGateway.updateActiveConfig(cfg);
            espNowGateway.sendConfigPacket(MSG_CONFIG_PREVIEW, cfg);
            Serial.printf("[CLI] Updated Normal Sleep Interval to %d sec.\n", val);
        }
    }
    else if (cmd.startsWith("set motor ")) {
        int val = cmd.substring(10).toInt();
        if (val >= 2) {
            ConfigPacket cfg = espNowGateway.getActiveConfig();
            cfg.motorMonitoringIntervalSeconds = (uint32_t)val;
            espNowGateway.updateActiveConfig(cfg);
            espNowGateway.sendConfigPacket(MSG_CONFIG_PREVIEW, cfg);
            Serial.printf("[CLI] Updated Motor Monitoring Interval to %d sec.\n", val);
        }
    }
    else if (cmd.startsWith("set low ")) {
        int val = cmd.substring(8).toInt();
        if (val >= 0 && val <= 100) {
            ConfigPacket cfg = espNowGateway.getActiveConfig();
            cfg.lowWaterThreshold = (uint8_t)val;
            espNowGateway.updateActiveConfig(cfg);
            espNowGateway.sendConfigPacket(MSG_CONFIG_PREVIEW, cfg);
            Serial.printf("[CLI] Updated Low Water Threshold to %d%%.\n", val);
        }
    }
    else if (cmd.startsWith("set max ")) {
        int val = cmd.substring(8).toInt();
        if (val > 0) {
            ConfigPacket cfg = espNowGateway.getActiveConfig();
            cfg.maxMotorRuntimeSeconds = (uint32_t)val;
            motorCtrl.setMaxRuntimeSeconds((uint32_t)val);
            espNowGateway.updateActiveConfig(cfg);
            espNowGateway.sendConfigPacket(MSG_CONFIG_PREVIEW, cfg);
            Serial.printf("[CLI] Updated Max Motor Runtime to %d sec.\n", val);
        }
    }
    else if (cmd == "save config") {
        ConfigPacket cfg = espNowGateway.getActiveConfig();
        cfg.configVersion++;
        espNowGateway.updateActiveConfig(cfg);
        espNowGateway.sendConfigPacket(MSG_CONFIG_SAVE, cfg);
        rxMqtt.publishConfigState(cfg);
        Serial.printf("[CLI] Incremented Config Version to %u & queued MSG_CONFIG_SAVE for Tx!\n", cfg.configVersion);
    }
    else {
        Serial.printf("[CLI] Unknown command: '%s'. Type 'help' for menu.\n", cmd.c_str());
    }
}
