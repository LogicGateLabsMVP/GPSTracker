#pragma once

#include "Config.h"
#include "DeviceModule.h"

#include <TinyGPSPlus.h>

class FLogger;

class FGPSModule : public IDeviceModule
{
public:
    explicit FGPSModule(FLogger& InLogger);
    ~FGPSModule() override;

    bool Initialize() override;
    void Update() override;

    bool IsUartStarted();
    uint32_t GetCharsProcessed();
    bool HasTraffic();
    bool HasValidFix();
    bool HasStoredFix();
    String GetShortStatus();
    String GetLatitudeString(uint8_t Precision = 6);
    String GetLongitudeString(uint8_t Precision = 6);
    String GetSatellitesString();
    String GetFixSummary();
    bool ConsumePersistableFix(FStoredGpsFixData& OutFix);
    void RestorePersistedFix(const FStoredGpsFixData& InFix);
    void ClearStoredFix();
    bool GetSmsCandidateCoordinates(double& OutLatitude, double& OutLongitude);
    String BuildSmsBody();

private:
    String BuildCurrentUtcString();
    void CacheCurrentValidFixNow();
    void CacheFixFromParser();
    void CacheFix(double InLatitude, double InLongitude, uint32_t InSatellites, bool bInSatellitesValid, const String& InUtcString);

private:
    TinyGPSPlus Parser;
    FLogger& Logger;
    uint32_t LastRxTimeMs;
    bool bUartStarted;
    bool bHasStoredFix;
    double StoredLatitude;
    double StoredLongitude;
    uint32_t StoredFixCaptureMs;
    uint32_t StoredSatellites;
    bool bStoredSatellitesValid;
    String StoredUtcString;
    bool bPersistDirty;
    uint32_t LastPersistMarkMs;
};
