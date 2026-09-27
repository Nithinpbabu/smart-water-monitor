#include "antenna_select.h"

#define PIN_RF_SWITCH_POWER 3
#define PIN_RF_ANT_SELECT    14

void configureTxAntenna(AntennaType type) {
    // Step 1: Enable RF switch IC power (Active LOW on GPIO 3)
    pinMode(PIN_RF_SWITCH_POWER, OUTPUT);
    digitalWrite(PIN_RF_SWITCH_POWER, LOW);
    delay(10); // Allow RF switch power supply to stabilize
    
    // Step 2: Configure RF antenna selector pin (GPIO 14)
    pinMode(PIN_RF_ANT_SELECT, OUTPUT);
    
    if (type == ANTENNA_EXTERNAL) {
        digitalWrite(PIN_RF_ANT_SELECT, HIGH);
        Serial.println("[Tx Antenna] RF Switch configured for EXTERNAL U.FL Antenna (GPIO3=LOW, GPIO14=HIGH).");
    } else {
        digitalWrite(PIN_RF_ANT_SELECT, LOW);
        Serial.println("[Tx Antenna] RF Switch configured for INTERNAL Ceramic Antenna (GPIO3=LOW, GPIO14=LOW).");
    }
}
