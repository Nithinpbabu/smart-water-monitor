#ifndef TX_WATER_SENSOR_H
#define TX_WATER_SENSOR_H

#include <Arduino.h>
#include "config.h"

// Hardware Pin Definitions for Xiao ESP32-C3
#define ECHO_PIN       D2
#define TRIG_PIN       D3
#define SENSOR_PWR_PIN D10

struct WaterReading {
    uint16_t distanceCm;   // Ultrasonic measured distance
    uint8_t  percentage;   // Calculated water %
    uint8_t  status;       // SENSOR_STATUS_OK, SENSOR_STATUS_GLITCH_FIX, etc.
};

class WaterSensor {
public:
    WaterSensor();
    void begin();
    WaterReading readSensor(const TankConfig& config);

private:
    uint16_t measureDistanceOnce();
    uint16_t measureAverageDistance(int samples, int delayMs);
};

extern WaterSensor waterSensor;

#endif // TX_WATER_SENSOR_H
