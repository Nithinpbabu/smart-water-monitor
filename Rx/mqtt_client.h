#ifndef RX_MQTT_CLIENT_H
#define RX_MQTT_CLIENT_H

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include "protocol.h"

class RxMqttClient {
public:
    RxMqttClient();
    void begin(const char* brokerIp, uint16_t brokerPort);
    void update();
    
    bool isConnected() { return _client.connected(); }
    
    void publishTelemetry(const SensorPacket& packet);
    void publishMotorEvent(const char* event, const char* status, const char* reason);
    void publishConfigState(const ConfigPacket& cfg);
    void publishLog(const char* node, const char* msg);
    void publishOtaCommand(const char* node, const char* cmd);

private:
    WiFiClient _espClient;
    PubSubClient _client;
    const char* _brokerIp;
    uint16_t _brokerPort;
    unsigned long _lastReconnectAttempt;
    
    void reconnect();
    static void callback(char* topic, byte* payload, unsigned int length);
};

extern RxMqttClient rxMqtt;

#endif // RX_MQTT_CLIENT_H
