#pragma once

#include "Config.h"
#include "DeviceModule.h"

#include <Preferences.h>

class FLogger;

class FConfigStorage : public IDeviceModule
{
public:
    explicit FConfigStorage(FLogger& InLogger);
    ~FConfigStorage() override;

    bool Initialize() override;
    void Update() override;
    void Deinitialize() override;

    bool Load(FDeviceConfig& OutConfig);
    bool Save(const FDeviceConfig& InConfig);
    bool ResetAllUserData();
    bool LoadPeriodicSettings(FPeriodicSmsSettings& OutSettings);
    bool SavePeriodicSettings(const FPeriodicSmsSettings& InSettings);
    bool LoadStoredGpsFix(FStoredGpsFixData& OutFix);
    bool SaveStoredGpsFix(const FStoredGpsFixData& InFix);
    bool LoadGpsBoundsSettings(FGpsBoundsSettings& OutSettings);
    bool SaveGpsBoundsSettings(const FGpsBoundsSettings& InSettings);

private:
    FLogger& Logger;
    Preferences Prefs;
};
