#ifndef TX_CONFIG_H
#define TX_CONFIG_H

#include <Arduino.h>
#include <Preferences.h>
#include "protocol.h"

struct TankConfig {
    uint32_t configVersion;
    uint16_t waterTopLevelCm;
    uint16_t waterBottomLevelCm;
    uint32_t normalSleepIntervalSeconds;
    uint32_t motorMonitoringIntervalSeconds;
    uint8_t  lowWaterThreshold;
    uint32_t maxMotorRuntimeSeconds;
};

class ConfigManager {
public:
    ConfigManager();
    void begin();
    
    TankConfig getActiveConfig() const { return _activeConfig; }
    TankConfig getEffectiveConfig() const;
    
    void saveConfig(const TankConfig& cfg);
    void applyPreviewConfig(const TankConfig& previewCfg);
    void clearPreviewConfig();
    bool hasPreviewConfig() const { return _hasPreview; }

private:
    TankConfig _activeConfig;
    TankConfig _previewConfig;
    bool _hasPreview;
    Preferences _prefs;
};

extern ConfigManager configMgr;

#endif // TX_CONFIG_H
