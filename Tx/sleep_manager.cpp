#include "sleep_manager.h"

SleepManager sleepMgr;

RTC_DATA_ATTR static uint32_t bootSequenceCount = 0;

SleepManager::SleepManager() {}

void SleepManager::begin() {
    bootSequenceCount++;
    Serial.printf("[SleepManager] Boot Sequence #%u\n", bootSequenceCount);
}

uint32_t SleepManager::getBootCount() const {
    return bootSequenceCount;
}

void SleepManager::incrementBootCount() {
    bootSequenceCount++;
}

void SleepManager::goToDeepSleep(uint32_t sleepSeconds) {
    if (sleepSeconds == 0) sleepSeconds = 300;
    
    Serial.printf("[SleepManager] Entering Deep Sleep for %u seconds...\n", sleepSeconds);
    Serial.flush();
    
    uint64_t sleepUs = (uint64_t)sleepSeconds * 1000000ULL;
    esp_sleep_enable_timer_wakeup(sleepUs);
    esp_deep_sleep_start();
}
