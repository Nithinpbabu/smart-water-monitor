#include "water_sensor.h"

WaterSensor waterSensor;

WaterSensor::WaterSensor() {}

void WaterSensor::begin() {
    pinMode(ULTRASONIC_TRIG_PIN, OUTPUT);
    pinMode(ULTRASONIC_ECHO_PIN, INPUT);
    digitalWrite(ULTRASONIC_TRIG_PIN, LOW);
}

WaterReading WaterSensor::readSensor(const TankConfig& cfg) {
    WaterReading reading;
    
    // Trigger ultrasonic pulse
    digitalWrite(ULTRASONIC_TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(ULTRASONIC_TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(ULTRASONIC_TRIG_PIN, LOW);
    
    // Read Echo pulse duration (timeout 30,000 us)
    unsigned long durationUs = pulseIn(ULTRASONIC_ECHO_PIN, HIGH, 30000);
    
    uint16_t distCm = 0;
    if (durationUs == 0) {
        distCm = cfg.waterBottomLevelCm; // Timeout fallback
        reading.status = SENSOR_STATUS_ERROR;
    } else {
        distCm = (uint16_t)(durationUs * 0.0343 / 2.0);
        reading.status = SENSOR_STATUS_OK;
    }
    
    reading.distanceCm = distCm;
    
    // Constrain distance to top..bottom bounds
    uint16_t topCm = cfg.waterTopLevelCm;
    uint16_t botCm = cfg.waterBottomLevelCm;
    if (botCm <= topCm) botCm = topCm + 1; // Prevent div by 0
    
    uint16_t constrainedDist = distCm;
    if (constrainedDist < topCm) constrainedDist = topCm;
    if (constrainedDist > botCm) constrainedDist = botCm;
    
    // Calculate percentage: topCm = 100%, botCm = 0%
    float pct = ((float)(botCm - constrainedDist) / (float)(botCm - topCm)) * 100.0f;
    if (pct < 0.0f) pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;
    
    reading.percentage = (uint8_t)pct;
    return reading;
}
