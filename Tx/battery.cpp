#include "battery.h"

BatteryMonitor batteryMon;

BatteryMonitor::BatteryMonitor() {}

void BatteryMonitor::begin() {
    analogReadResolution(12); // 12-bit ADC (0..4095)
    pinMode(BATTERY_ADC_PIN, INPUT);
}

BatteryReading BatteryMonitor::readBattery() {
    BatteryReading reading;
    
    // 1. Dummy ADC read to clear SAR ADC sample capacitor
    analogRead(BATTERY_ADC_PIN);
    delay(2);
    
    // 2. Average 100 ADC samples (matching Section 24/25 hardware architecture spec)
    const int NUM_READINGS = 100;
    uint32_t adcSum = 0;
    for (int i = 0; i < NUM_READINGS; i++) {
        adcSum += analogRead(BATTERY_ADC_PIN);
        delayMicroseconds(100);
    }
    uint16_t avgAdc = adcSum / NUM_READINGS;
    
    // 3. Voltage calculation with voltage divider (R1=R2=100k) & hardware calibration factor (1.237)
    float pinVolts = ((float)avgAdc * 3.3f) / 4095.0f;
    float batVolts = pinVolts * 2.0f * 1.237f;
    uint16_t batMv = (uint16_t)(batVolts * 1000.0f);
    
    // 4. Percentage mapping: 2.9V (0%) .. 4.0V (100%)
    float constrainedBatV = batVolts;
    if (constrainedBatV < 2.9f) constrainedBatV = 2.9f;
    if (constrainedBatV > 4.0f) constrainedBatV = 4.0f;
    
    float pctFloat = ((constrainedBatV - 2.9f) / (4.0f - 2.9f)) * 100.0f;
    if (pctFloat < 0.0f) pctFloat = 0.0f;
    if (pctFloat > 100.0f) pctFloat = 100.0f;
    
    reading.rawAdc = avgAdc;
    reading.voltageMv = batMv;
    reading.percentage = (uint8_t)pctFloat;
    
    return reading;
}
