#ifndef TX_BATTERY_H
#define TX_BATTERY_H

#include <Arduino.h>

#define BATTERY_ADC_PIN 2

struct BatteryReading {
    uint16_t rawAdc;
    uint16_t voltageMv;
    uint8_t  percentage;
};

class BatteryMonitor {
public:
    BatteryMonitor();
    void begin();
    BatteryReading readBattery();
};

extern BatteryMonitor batteryMon;

#endif // TX_BATTERY_H
