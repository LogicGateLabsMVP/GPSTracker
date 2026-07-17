#pragma once

#include <Arduino.h>
#include <string.h>

#include <stdint.h>

enum class EAfterInitAction : uint8_t
{
    None,
    StatusRefresh,
    SendSms
};

namespace HW
{
    inline constexpr uint8_t GpsTx = 5;
    inline constexpr uint8_t GpsRx = 4;

    inline constexpr uint8_t I2cSda = 12;
    inline constexpr uint8_t I2cScl = 13;

    inline constexpr uint8_t GsmTx = 14;
    inline constexpr uint8_t GsmRx = 22;
    inline constexpr uint8_t GsmBoot = 3;

    inline constexpr uint8_t KeyUp   = 0;
    inline constexpr uint8_t KeyDown = 1;
    inline constexpr uint8_t KeyOk   = 10;
    inline constexpr uint8_t KeyBack = 11;
}

namespace AppConfig
{
    inline constexpr bool ButtonsActiveLow = true;
    inline constexpr bool GsmBootEnableLevelLow = false;

    inline constexpr uint32_t UsbLogBaud = 115200;
    inline constexpr uint32_t GpsBaud = 9600;

    inline constexpr uint32_t ButtonDebounceMs = 40;
    inline constexpr uint32_t ScreenRefreshMs = 80;
    inline constexpr uint32_t GpsFixMaxAgeMs = 30000;
    inline constexpr uint32_t GpsWarmupProbeMs = 2000;
    inline constexpr uint32_t GpsDiagnosticsFixWaitMs = 120000;
    inline constexpr uint32_t GsmStatusRefreshMs = 1000;
    inline constexpr uint32_t GpsPersistMinIntervalMs = 15000;
    inline constexpr uint32_t GsmPowerSettleMs = 1500;
    inline constexpr uint32_t GsmSoftPowerOffTimeoutMs = 8000;
    inline constexpr uint32_t GsmSoftPowerOffGraceMs = 2500;
    inline constexpr uint32_t GsmPeriodicRetryMs = 20000;
    inline constexpr uint32_t DisplayProbeMs = 1000;
    inline constexpr uint32_t DisplayI2cHz = 100000;

    inline constexpr uint32_t DefaultPeriodicSmsSeconds = 30;
    inline constexpr uint32_t MinPeriodicSmsSeconds = 5;
    inline constexpr uint32_t MaxPeriodicSmsSeconds = 86400;
    inline constexpr uint32_t PeriodicSmsStepSeconds = 5;

    inline constexpr char PreferencesNamespace[] = "tracker";
    inline constexpr char RecipientKey[] = "recipient";
    inline constexpr char DefaultRecipient[] = "+380000000000";
    inline constexpr char PeriodicEnabledKey[] = "psms_en";
    inline constexpr char PeriodicPeriodKey[] = "psms_sec";
    inline constexpr char GpsHasFixKey[] = "gps_has";
    inline constexpr char GpsLatKey[] = "gps_lat";
    inline constexpr char GpsLngKey[] = "gps_lng";
    inline constexpr char GpsSatKey[] = "gps_sat";
    inline constexpr char GpsSatValidKey[] = "gps_satv";
    inline constexpr char GpsUtcKey[] = "gps_utc";
    inline constexpr char PrivateModeKey[] = "priv_en";
    inline constexpr char PrivatePasswordKey[] = "priv_pwd";
    inline constexpr char GpsBoundsEnabledKey[] = "gb_en";
    inline constexpr char GpsBound1LatKey[] = "gb1_lat";
    inline constexpr char GpsBound1LngKey[] = "gb1_lng";
    inline constexpr char GpsBound2LatKey[] = "gb2_lat";
    inline constexpr char GpsBound2LngKey[] = "gb2_lng";
    inline constexpr char GpsBound3LatKey[] = "gb3_lat";
    inline constexpr char GpsBound3LngKey[] = "gb3_lng";
    inline constexpr char GpsBound4LatKey[] = "gb4_lat";
    inline constexpr char GpsBound4LngKey[] = "gb4_lng";
}

enum class ELogLevel : uint8_t
{
    Info,
    Warning,
    Error
};

enum class EButtonEvent : uint8_t
{
    None,
    Up,
    Down,
    Ok,
    Back
};

enum class EScreen : uint8_t
{
    Diagnostics,
    Menu,
    Status,
    Logs,
    GpsDiagnostics,
    EditRecipient,
    PeriodicSmsSettings,
    GpsBoundsMenu,
    GpsBoundsEdit,
    PrivateModeMenu,
    PasswordEntry,
    SpaceImpact,
    Message
};

enum class EPasswordPurpose : uint8_t
{
    None,
    EnablePrivateMode,
    DisablePrivateMode
};

struct FDeviceConfig
{
    char Recipient[20];
    bool bPrivateMode = false;
    char PrivatePassword[8];
};

struct FGsmStatus
{
    bool bResponsive = false;
    String SimState = "Unknown";
    String NetworkState = "Unknown";
    String OperatorName = "Unknown";
    String Iccid = "N/A";
    int32_t Csq = -1;
};

struct FPeriodicSmsSettings
{
    bool bEnabled = false;
    uint32_t PeriodSeconds = AppConfig::DefaultPeriodicSmsSeconds;
};

struct FStoredGpsFixData
{
    bool bHasFix = false;
    double Latitude = 0.0;
    double Longitude = 0.0;
    uint32_t Satellites = 0;
    bool bSatellitesValid = false;
    char Utc[24];
};

struct FGpsBoundPoint
{
    double Latitude = 0.0;
    double Longitude = 0.0;
};

struct FGpsBoundsSettings
{
    bool bUseBounds = false;
    FGpsBoundPoint Points[4];
};

static String FormatCoordinate(double Value, uint8_t Precision)
{
    char Buffer[32];
    snprintf(Buffer, sizeof(Buffer), "%.*f", static_cast<int>(Precision), Value);
    return String(Buffer);
}

static String FormatSeconds(uint32_t Milliseconds, uint8_t Precision)
{
    return FormatCoordinate(static_cast<double>(Milliseconds) / 1000.0, Precision);
}

