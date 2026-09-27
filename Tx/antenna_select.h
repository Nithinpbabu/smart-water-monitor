#ifndef TX_ANTENNA_SELECT_H
#define TX_ANTENNA_SELECT_H

#include <Arduino.h>

enum AntennaType {
    ANTENNA_INTERNAL,
    ANTENNA_EXTERNAL
};

// Configure the RF switch IC (GPIO3 = Power enable active LOW, GPIO14 = LOW for internal, HIGH for external)
void configureTxAntenna(AntennaType type);

#endif // TX_ANTENNA_SELECT_H
