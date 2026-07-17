#include "Storage.h"

#include "Logger.h"

#include <Preferences.h>

FConfigStorage::FConfigStorage(FLogger& InLogger)
    : Logger(InLogger)
{
}

FConfigStorage::~FConfigStorage() = default;

bool FConfigStorage::Initialize()
{
    if (!Prefs.begin(AppConfig::PreferencesNamespace, false))
    {
        Logger.Log(ELogLevel::Error, "Preferences init failed.");
        return false;
    }

    Logger.Log(ELogLevel::Info, "Preferences ready.");
    return true;
}

void FConfigStorage::Update()
{
}

void FConfigStorage::Deinitialize()
{
    Prefs.end();
}

bool FConfigStorage::Load(FDeviceConfig& OutConfig)
{
    memset(&OutConfig, 0, sizeof(OutConfig));
    strlcpy(OutConfig.Recipient, AppConfig::DefaultRecipient, sizeof(OutConfig.Recipient));
    OutConfig.bPrivateMode = Prefs.getBool(AppConfig::PrivateModeKey, false);

    const String Value = Prefs.getString(AppConfig::RecipientKey, AppConfig::DefaultRecipient);
    strlcpy(OutConfig.Recipient, Value.c_str(), sizeof(OutConfig.Recipient));

    const String Password = Prefs.getString(AppConfig::PrivatePasswordKey, "");
    strlcpy(OutConfig.PrivatePassword, Password.c_str(), sizeof(OutConfig.PrivatePassword));

    Logger.Log(ELogLevel::Info, "Recipient loaded: " + String(OutConfig.Recipient));
    Logger.Log(ELogLevel::Info, String("Private mode loaded: ") + (OutConfig.bPrivateMode ? "ON" : "OFF"));
    return true;
}

bool FConfigStorage::Save(const FDeviceConfig& InConfig)
{
    const bool bRecipientOk = Prefs.putString(AppConfig::RecipientKey, InConfig.Recipient) >= 0;
    const bool bPrivateOk = Prefs.putBool(AppConfig::PrivateModeKey, InConfig.bPrivateMode);
    const bool bPasswordOk = Prefs.putString(AppConfig::PrivatePasswordKey, InConfig.PrivatePassword) >= 0;
    if (!bRecipientOk || !bPrivateOk || !bPasswordOk)
    {
        Logger.Log(ELogLevel::Error, "Config save failed.");
        return false;
    }

    Logger.Log(ELogLevel::Info, "Recipient saved: " + String(InConfig.Recipient));
    Logger.Log(ELogLevel::Info, String("Private mode saved: ") + (InConfig.bPrivateMode ? "ON" : "OFF"));
    return true;
}

bool FConfigStorage::ResetAllUserData()
{
    if (!Prefs.clear())
    {
        Logger.Log(ELogLevel::Error, "Reset all data failed.");
        return false;
    }

    Prefs.putString(AppConfig::RecipientKey, "");
    Prefs.putBool(AppConfig::PeriodicEnabledKey, false);
    Prefs.putUInt(AppConfig::PeriodicPeriodKey, AppConfig::DefaultPeriodicSmsSeconds);
    Prefs.putBool(AppConfig::GpsHasFixKey, false);
    Prefs.putString(AppConfig::GpsLatKey, "");
    Prefs.putString(AppConfig::GpsLngKey, "");
    Prefs.putUInt(AppConfig::GpsSatKey, 0);
    Prefs.putBool(AppConfig::GpsSatValidKey, false);
    Prefs.putString(AppConfig::GpsUtcKey, "");
    Prefs.putBool(AppConfig::PrivateModeKey, false);
    Prefs.putString(AppConfig::PrivatePasswordKey, "");
    Prefs.putBool(AppConfig::GpsBoundsEnabledKey, false);
    Prefs.putString(AppConfig::GpsBound1LatKey, "0.0000");
    Prefs.putString(AppConfig::GpsBound1LngKey, "0.0000");
    Prefs.putString(AppConfig::GpsBound2LatKey, "0.0000");
    Prefs.putString(AppConfig::GpsBound2LngKey, "0.0000");
    Prefs.putString(AppConfig::GpsBound3LatKey, "0.0000");
    Prefs.putString(AppConfig::GpsBound3LngKey, "0.0000");
    Prefs.putString(AppConfig::GpsBound4LatKey, "0.0000");
    Prefs.putString(AppConfig::GpsBound4LngKey, "0.0000");

    Logger.Log(ELogLevel::Warning, "All user data reset to defaults.");
    return true;
}

bool FConfigStorage::LoadPeriodicSettings(FPeriodicSmsSettings& OutSettings)
{
    OutSettings.bEnabled = Prefs.getBool(AppConfig::PeriodicEnabledKey, false);
    OutSettings.PeriodSeconds = Prefs.getUInt(AppConfig::PeriodicPeriodKey, AppConfig::DefaultPeriodicSmsSeconds);
    if (OutSettings.PeriodSeconds < AppConfig::MinPeriodicSmsSeconds)
    {
        OutSettings.PeriodSeconds = AppConfig::MinPeriodicSmsSeconds;
    }
    if (OutSettings.PeriodSeconds > AppConfig::MaxPeriodicSmsSeconds)
    {
        OutSettings.PeriodSeconds = AppConfig::MaxPeriodicSmsSeconds;
    }
    Logger.Log(ELogLevel::Info, String("Periodic SMS loaded: ") + (OutSettings.bEnabled ? "ON" : "OFF") + ", " + String(OutSettings.PeriodSeconds) + " s.");
    return true;
}

bool FConfigStorage::SavePeriodicSettings(const FPeriodicSmsSettings& InSettings)
{
    const bool bA = Prefs.putBool(AppConfig::PeriodicEnabledKey, InSettings.bEnabled);
    const size_t bB = Prefs.putUInt(AppConfig::PeriodicPeriodKey, InSettings.PeriodSeconds);
    if (!bA || bB == 0)
    {
        Logger.Log(ELogLevel::Error, "Periodic SMS settings save failed.");
        return false;
    }

    Logger.Log(ELogLevel::Info, String("Periodic SMS saved: ") + (InSettings.bEnabled ? "ON" : "OFF") + ", " + String(InSettings.PeriodSeconds) + " s.");
    return true;
}

bool FConfigStorage::LoadStoredGpsFix(FStoredGpsFixData& OutFix)
{
    memset(&OutFix, 0, sizeof(OutFix));
    OutFix.bHasFix = Prefs.getBool(AppConfig::GpsHasFixKey, false);
    if (!OutFix.bHasFix)
    {
        Logger.Log(ELogLevel::Info, "Stored GPS fix not found.");
        return false;
    }

    const String Lat = Prefs.getString(AppConfig::GpsLatKey, "");
    const String Lng = Prefs.getString(AppConfig::GpsLngKey, "");
    if (Lat.length() == 0 || Lng.length() == 0)
    {
        Logger.Log(ELogLevel::Warning, "Stored GPS fix is incomplete.");
        return false;
    }

    OutFix.Latitude = Lat.toDouble();
    OutFix.Longitude = Lng.toDouble();
    OutFix.Satellites = Prefs.getUInt(AppConfig::GpsSatKey, 0);
    OutFix.bSatellitesValid = Prefs.getBool(AppConfig::GpsSatValidKey, false);
    const String Utc = Prefs.getString(AppConfig::GpsUtcKey, "");
    strlcpy(OutFix.Utc, Utc.c_str(), sizeof(OutFix.Utc));
    Logger.Log(ELogLevel::Info, "Stored GPS fix loaded.");
    return true;
}

bool FConfigStorage::SaveStoredGpsFix(const FStoredGpsFixData& InFix)
{
    if (!InFix.bHasFix)
    {
        return false;
    }

    const String Lat = FormatCoordinate(InFix.Latitude, 6);
    const String Lng = FormatCoordinate(InFix.Longitude, 6);

    bool bOk = true;
    bOk &= Prefs.putBool(AppConfig::GpsHasFixKey, true);
    bOk &= Prefs.putString(AppConfig::GpsLatKey, Lat) > 0;
    bOk &= Prefs.putString(AppConfig::GpsLngKey, Lng) > 0;
    bOk &= Prefs.putUInt(AppConfig::GpsSatKey, InFix.Satellites) > 0 || InFix.Satellites == 0;
    bOk &= Prefs.putBool(AppConfig::GpsSatValidKey, InFix.bSatellitesValid);
    bOk &= Prefs.putString(AppConfig::GpsUtcKey, String(InFix.Utc)) >= 0;

    if (!bOk)
    {
        Logger.Log(ELogLevel::Error, "Stored GPS fix save failed.");
        return false;
    }

    return true;
}

bool FConfigStorage::LoadGpsBoundsSettings(FGpsBoundsSettings& OutSettings)
{
    OutSettings = FGpsBoundsSettings{};
    OutSettings.bUseBounds = Prefs.getBool(AppConfig::GpsBoundsEnabledKey, false);

    const char* LatKeys[4] =
    {
        AppConfig::GpsBound1LatKey,
        AppConfig::GpsBound2LatKey,
        AppConfig::GpsBound3LatKey,
        AppConfig::GpsBound4LatKey
    };

    const char* LngKeys[4] =
    {
        AppConfig::GpsBound1LngKey,
        AppConfig::GpsBound2LngKey,
        AppConfig::GpsBound3LngKey,
        AppConfig::GpsBound4LngKey
    };

    for (uint8_t Index = 0; Index < 4; ++Index)
    {
        OutSettings.Points[Index].Latitude = Prefs.getString(LatKeys[Index], "0.0000").toDouble();
        OutSettings.Points[Index].Longitude = Prefs.getString(LngKeys[Index], "0.0000").toDouble();
    }

    Logger.Log(ELogLevel::Info, String("GPS bounds loaded: ") + (OutSettings.bUseBounds ? "ON" : "OFF"));
    return true;
}

bool FConfigStorage::SaveGpsBoundsSettings(const FGpsBoundsSettings& InSettings)
{
    bool bOk = Prefs.putBool(AppConfig::GpsBoundsEnabledKey, InSettings.bUseBounds);

    const char* LatKeys[4] =
    {
        AppConfig::GpsBound1LatKey,
        AppConfig::GpsBound2LatKey,
        AppConfig::GpsBound3LatKey,
        AppConfig::GpsBound4LatKey
    };

    const char* LngKeys[4] =
    {
        AppConfig::GpsBound1LngKey,
        AppConfig::GpsBound2LngKey,
        AppConfig::GpsBound3LngKey,
        AppConfig::GpsBound4LngKey
    };

    for (uint8_t Index = 0; Index < 4; ++Index)
    {
        bOk &= Prefs.putString(LatKeys[Index], FormatCoordinate(InSettings.Points[Index].Latitude, 4)) >= 0;
        bOk &= Prefs.putString(LngKeys[Index], FormatCoordinate(InSettings.Points[Index].Longitude, 4)) >= 0;
    }

    if (!bOk)
    {
        Logger.Log(ELogLevel::Error, "GPS bounds save failed.");
        return false;
    }

    Logger.Log(ELogLevel::Info, String("GPS bounds saved: ") + (InSettings.bUseBounds ? "ON" : "OFF"));
    return true;
}

