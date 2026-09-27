#include "battery.h"

BatteryMonitor batteryMon;

BatteryMonitor::BatteryMonitor() {}

void BatteryMonitor::begin() {
    analogReadResolution(12); // 12-bit ADC (0..4095)
}

BatteryReading BatteryMonitor::readBattery() {
    BatteryReading reading;
    
    // Read ADC on BATTERY_ADC_PIN
    uint32_t adcSum = 0;
    for (int i = 0; i < 10; i++) {
        adcSum += analogRead(BATTERY_ADC_PIN);
        delay(2);
    }
    uint16_t avgAdc = adcSum / 10;
    
    // Voltage divider ratio calculation (2.0 factor for equal R1/R2)
    float pinVolts = (avgAdc / 4095.0f) * 3.3f;
    float batVolts = pinVolts * 2.0f;
    uint16_t batMv = (uint16_t)(batVolts * 1000.0f);
    
    // Percentage 3.0V (0%) .. 4.2V (100%)
    uint8_t pct = 0;
    if (batMv > 3000) {
        float p = ((float)(batMv - 3000) / 1200.0f) * 100.0f;
        if (p > 100.0f) p = 100.0f;
        pct = (uint8_t)p;
    }
    
    reading.rawAdc = avgAdc;
    reading.voltageMv = batMv;
    reading.percentage = pct;
    
    return reading;
}
