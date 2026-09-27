#ifndef TX_SLEEP_MANAGER_H
#define TX_SLEEP_MANAGER_H

#include <Arduino.h>

class SleepManager {
public:
    SleepManager();
    void begin();
    
    uint32_t getBootCount() const;
    void incrementBootCount();
    
    void goToDeepSleep(uint32_t sleepSeconds);
};

extern SleepManager sleepMgr;

#endif // TX_SLEEP_MANAGER_H
