#include "water_sensor.h"

WaterSensor waterSensor;

WaterSensor::WaterSensor() {}

void WaterSensor::begin() {
    pinMode(ULTRASONIC_TRIG_PIN, OUTPUT);
    pinMode(ULTRASONIC_ECHO_PIN, INPUT);
    pinMode(SENSOR_PWR_PIN, OUTPUT);
    
    digitalWrite(ULTRASONIC_TRIG_PIN, LOW);
    digitalWrite(SENSOR_PWR_PIN, LOW);
}

uint16_t WaterSensor::measureDistanceOnce() {
    digitalWrite(ULTRASONIC_TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(ULTRASONIC_TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(ULTRASONIC_TRIG_PIN, LOW);
    
    unsigned long durationUs = pulseIn(ULTRASONIC_ECHO_PIN, HIGH, 30000);
    if (durationUs == 0) return 0;
    return (uint16_t)(durationUs * 0.0343 / 2.0);
}

uint16_t WaterSensor::measureAverageDistance(int samples, int delayMs) {
    uint32_t totalDist = 0;
    int validSamples = 0;
    
    for (int i = 0; i < samples; i++) {
        uint16_t dist = measureDistanceOnce();
        if (dist > 0) {
            totalDist += dist;
            validSamples++;
        }
        if (delayMs > 0) delay(delayMs);
    }
    
    if (validSamples == 0) return 0;
    return (uint16_t)(totalDist / validSamples);
}

WaterReading WaterSensor::readSensor(const TankConfig& cfg) {
    WaterReading reading;
    
    // 1. Energize SENSOR_PWR_PIN (D10) and wait 50ms for ultrasonic module to power up & stabilize
    digitalWrite(SENSOR_PWR_PIN, HIGH);
    delay(50);
    
    // 2. Take 10 distance samples with 5ms inter-sample delay
    uint16_t distCm = measureAverageDistance(10, 5);
    
    // 3. Power OFF sensor immediately after measurement to save battery
    digitalWrite(SENSOR_PWR_PIN, LOW);
    
    if (distCm == 0) {
        distCm = cfg.waterBottomLevelCm; // Timeout/error fallback
        reading.status = SENSOR_STATUS_ERROR;
    } else {
        reading.status = SENSOR_STATUS_OK;
    }
    
    reading.distanceCm = distCm;
    
    // 4. Calculate water percentage based on calibrated top & bottom levels
    uint16_t topCm = cfg.waterTopLevelCm;
    uint16_t botCm = cfg.waterBottomLevelCm;
    if (botCm <= topCm) botCm = topCm + 1; // Prevent div by 0
    
    uint16_t constrainedDist = distCm;
    if (constrainedDist < topCm) constrainedDist = topCm;
    if (constrainedDist > botCm) constrainedDist = botCm;
    
    float pct = ((float)(botCm - constrainedDist) / (float)(botCm - topCm)) * 100.0f;
    if (pct < 0.0f) pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;
    
    reading.percentage = (uint8_t)pct;
    return reading;
}
