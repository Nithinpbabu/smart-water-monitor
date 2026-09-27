#include "config.h"

ConfigManager configMgr;

ConfigManager::ConfigManager() : _hasPreview(false) {
    _activeConfig.configVersion = 1;
    _activeConfig.waterTopLevelCm = 25;
    _activeConfig.waterBottomLevelCm = 150;
    _activeConfig.normalSleepIntervalSeconds = 300;
    _activeConfig.motorMonitoringIntervalSeconds = 10;
    _activeConfig.lowWaterThreshold = 20;
    _activeConfig.maxMotorRuntimeSeconds = 3600;
}

void ConfigManager::begin() {
    _prefs.begin("tank_cfg", false);
    
    _activeConfig.configVersion = _prefs.getUInt("ver", 1);
    _activeConfig.waterTopLevelCm = _prefs.getUShort("top", 25);
    _activeConfig.waterBottomLevelCm = _prefs.getUShort("bot", 150);
    _activeConfig.normalSleepIntervalSeconds = _prefs.getUInt("norm_sec", 300);
    _activeConfig.motorMonitoringIntervalSeconds = _prefs.getUInt("mot_sec", 10);
    _activeConfig.lowWaterThreshold = _prefs.getUChar("low_th", 20);
    _activeConfig.maxMotorRuntimeSeconds = _prefs.getUInt("max_run", 3600);
    
    _prefs.end();
    
    Serial.printf("[Config] Loaded NVS config v%u: Top=%ucm, Bot=%ucm, Norm=%us, Motor=%us\n",
                  _activeConfig.configVersion, _activeConfig.waterTopLevelCm,
                  _activeConfig.waterBottomLevelCm, _activeConfig.normalSleepIntervalSeconds,
                  _activeConfig.motorMonitoringIntervalSeconds);
}

TankConfig ConfigManager::getEffectiveConfig() const {
    if (_hasPreview) {
        return _previewConfig;
    }
    return _activeConfig;
}

void ConfigManager::saveConfig(const TankConfig& cfg) {
    _activeConfig = cfg;
    _hasPreview = false;
    
    _prefs.begin("tank_cfg", false);
    _prefs.putUInt("ver", _activeConfig.configVersion);
    _prefs.putUShort("top", _activeConfig.waterTopLevelCm);
    _prefs.putUShort("bot", _activeConfig.waterBottomLevelCm);
    _prefs.putUInt("norm_sec", _activeConfig.normalSleepIntervalSeconds);
    _prefs.putUInt("mot_sec", _activeConfig.motorMonitoringIntervalSeconds);
    _prefs.putUChar("low_th", _activeConfig.lowWaterThreshold);
    _prefs.putUInt("max_run", _activeConfig.maxMotorRuntimeSeconds);
    _prefs.end();
    
    Serial.printf("[Config] Saved permanent config v%u to NVS Flash! Top=%ucm, Bot=%ucm\n",
                  _activeConfig.configVersion, _activeConfig.waterTopLevelCm, _activeConfig.waterBottomLevelCm);
}

void ConfigManager::applyPreviewConfig(const TankConfig& previewCfg) {
    _previewConfig = previewCfg;
    _hasPreview = true;
    Serial.printf("[Config] Applied TEMPORARY preview config (Top=%ucm, Bot=%ucm)\n",
                  _previewConfig.waterTopLevelCm, _previewConfig.waterBottomLevelCm);
}

void ConfigManager::clearPreviewConfig() {
    _hasPreview = false;
    Serial.println("[Config] Cleared temporary preview config.");
}
