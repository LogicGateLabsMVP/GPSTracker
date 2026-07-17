#include "GPSModule.h"

#include "Logger.h"

#include <TinyGPSPlus.h>

FGPSModule::FGPSModule(FLogger& InLogger)
    : Logger(InLogger)
    , LastRxTimeMs(0)
    , bUartStarted(false)
    , bHasStoredFix(false)
    , StoredLatitude(0.0)
    , StoredLongitude(0.0)
    , StoredFixCaptureMs(0)
    , StoredSatellites(0)
    , bStoredSatellitesValid(false)
    , bPersistDirty(false)
    , LastPersistMarkMs(0)
{
}

FGPSModule::~FGPSModule() = default;

bool FGPSModule::Initialize()
{
    Serial0.end();
    Serial0.setRxBufferSize(1024);
    Serial0.begin(AppConfig::GpsBaud, SERIAL_8N1, HW::GpsRx, HW::GpsTx);

    bUartStarted = true;
    LastRxTimeMs = 0;
    bPersistDirty = false;
    Logger.Log(ELogLevel::Info, "GPS UART0 started at 9600.");
    return true;
}

void FGPSModule::Update()
{
    while (Serial0.available() > 0)
    {
        const char Character = static_cast<char>(Serial0.read());
        Parser.encode(Character);
        if (Parser.location.isValid() && Parser.location.isUpdated())
        {
            CacheFixFromParser();
        }
        LastRxTimeMs = millis();
    }
}

bool FGPSModule::IsUartStarted()
{
    return bUartStarted;
}

uint32_t FGPSModule::GetCharsProcessed()
{
    return Parser.charsProcessed();
}

bool FGPSModule::HasTraffic()
{
    return Parser.charsProcessed() > 10 || (LastRxTimeMs != 0 && (millis() - LastRxTimeMs) < 1000);
}

bool FGPSModule::HasValidFix()
{
    return Parser.location.isValid() && Parser.location.age() < AppConfig::GpsFixMaxAgeMs;
}

bool FGPSModule::HasStoredFix()
{
    return bHasStoredFix;
}

String FGPSModule::GetShortStatus()
{
    if (HasValidFix())
    {
        return "Fix OK: " + GetLatitudeString(6) + "," + GetLongitudeString(6);
    }

    if (bHasStoredFix)
    {
        return "Stored: " + FormatCoordinate(StoredLatitude, 6) + "," + FormatCoordinate(StoredLongitude, 6);
    }

    if (!HasTraffic())
    {
        return "No GPS UART data";
    }

    return "GPS alive, no fix yet";
}

String FGPSModule::GetLatitudeString(uint8_t Precision)
{
    if (Parser.location.isValid())
    {
        return FormatCoordinate(Parser.location.lat(), Precision);
    }

    if (bHasStoredFix)
    {
        return FormatCoordinate(StoredLatitude, Precision);
    }

    return "n/a";
}

String FGPSModule::GetLongitudeString(uint8_t Precision)
{
    if (Parser.location.isValid())
    {
        return FormatCoordinate(Parser.location.lng(), Precision);
    }

    if (bHasStoredFix)
    {
        return FormatCoordinate(StoredLongitude, Precision);
    }

    return "n/a";
}

String FGPSModule::GetSatellitesString()
{
    if (Parser.satellites.isValid())
    {
        return String(Parser.satellites.value());
    }

    if (bHasStoredFix && bStoredSatellitesValid)
    {
        return String(StoredSatellites);
    }

    return "n/a";
}

String FGPSModule::GetFixSummary()
{
    if (Parser.location.isValid())
    {
        String Summary = "Age ";
        Summary += String(Parser.location.age());
        Summary += " ms";

        if (Parser.satellites.isValid())
        {
            Summary += " Sat ";
            Summary += String(Parser.satellites.value());
        }

        return Summary;
    }

    if (bHasStoredFix)
    {
        String Summary = "Stored Age ";
        Summary += String((millis() - StoredFixCaptureMs + 999u) / 1000u);
        Summary += " s";

        if (bStoredSatellitesValid)
        {
            Summary += " Sat ";
            Summary += String(StoredSatellites);
        }

        return Summary;
    }

    return "No fix";
}

bool FGPSModule::ConsumePersistableFix(FStoredGpsFixData& OutFix)
{
    if (!bPersistDirty || !bHasStoredFix)
    {
        return false;
    }

    memset(&OutFix, 0, sizeof(OutFix));
    OutFix.bHasFix = true;
    OutFix.Latitude = StoredLatitude;
    OutFix.Longitude = StoredLongitude;
    OutFix.Satellites = StoredSatellites;
    OutFix.bSatellitesValid = bStoredSatellitesValid;
    strlcpy(OutFix.Utc, StoredUtcString.c_str(), sizeof(OutFix.Utc));
    bPersistDirty = false;
    return true;
}

void FGPSModule::RestorePersistedFix(const FStoredGpsFixData& InFix)
{
    if (!InFix.bHasFix)
    {
        return;
    }

    bHasStoredFix = true;
    StoredLatitude = InFix.Latitude;
    StoredLongitude = InFix.Longitude;
    StoredFixCaptureMs = millis();
    StoredSatellites = InFix.Satellites;
    bStoredSatellitesValid = InFix.bSatellitesValid;
    StoredUtcString = String(InFix.Utc);
    Logger.Log(ELogLevel::Info, "GPS persisted fix restored.");
}

void FGPSModule::ClearStoredFix()
{
    bHasStoredFix = false;
    StoredLatitude = 0.0;
    StoredLongitude = 0.0;
    StoredFixCaptureMs = 0;
    StoredSatellites = 0;
    bStoredSatellitesValid = false;
    StoredUtcString = "";
    bPersistDirty = false;
}

bool FGPSModule::GetSmsCandidateCoordinates(double& OutLatitude, double& OutLongitude)
{
    if (HasValidFix())
    {
        CacheCurrentValidFixNow();
        OutLatitude = Parser.location.lat();
        OutLongitude = Parser.location.lng();
        return true;
    }

    if (bHasStoredFix)
    {
        OutLatitude = StoredLatitude;
        OutLongitude = StoredLongitude;
        return true;
    }

    return false;
}

String FGPSModule::BuildSmsBody()
{
    if (HasValidFix())
    {
        CacheCurrentValidFixNow();

        const String Latitude = GetLatitudeString(6);
        const String Longitude = GetLongitudeString(6);
        const uint32_t FixAgeSeconds = (Parser.location.age() + 999u) / 1000u;

        String Body = "https://maps.google.com/?q=";
        Body += Latitude;
        Body += ",";
        Body += Longitude;
        Body += "Age: ";
        Body += String(FixAgeSeconds);
        Body += "s";
        return Body;
    }

    if (bHasStoredFix)
    {
        const String Latitude = FormatCoordinate(StoredLatitude, 6);
        const String Longitude = FormatCoordinate(StoredLongitude, 6);
        const uint32_t StoredAgeSeconds = ((millis() - StoredFixCaptureMs) + 999u) / 1000u;

        String Body = "https://maps.google.com/?q=";
        Body += Latitude;
        Body += ",";
        Body += Longitude;
        Body += "Age: ";
        Body += String(StoredAgeSeconds);
        Body += "s";
        return Body;
    }

    return "No GPS data";
}

String FGPSModule::BuildCurrentUtcString()
{
    if (!Parser.date.isValid() || !Parser.time.isValid())
    {
        return "";
    }

    char Buffer[32];
    snprintf(
        Buffer,
        sizeof(Buffer),
        "%04d-%02d-%02d %02d:%02d:%02d",
        Parser.date.year(),
        Parser.date.month(),
        Parser.date.day(),
        Parser.time.hour(),
        Parser.time.minute(),
        Parser.time.second()
    );
    return String(Buffer);
}

void FGPSModule::CacheCurrentValidFixNow()
{
    if (!Parser.location.isValid())
    {
        return;
    }

    CacheFix(Parser.location.lat(), Parser.location.lng(), Parser.satellites.isValid() ? Parser.satellites.value() : 0, Parser.satellites.isValid(), BuildCurrentUtcString());
}

void FGPSModule::CacheFixFromParser()
{
    CacheFix(Parser.location.lat(), Parser.location.lng(), Parser.satellites.isValid() ? Parser.satellites.value() : 0, Parser.satellites.isValid(), BuildCurrentUtcString());
}

void FGPSModule::CacheFix(double InLatitude, double InLongitude, uint32_t InSatellites, bool bInSatellitesValid, const String& InUtcString)
{
    const bool bChanged = !bHasStoredFix ||
        StoredLatitude != InLatitude ||
        StoredLongitude != InLongitude ||
        StoredSatellites != InSatellites ||
        bStoredSatellitesValid != bInSatellitesValid ||
        StoredUtcString != InUtcString;

    bHasStoredFix = true;
    StoredLatitude = InLatitude;
    StoredLongitude = InLongitude;
    StoredFixCaptureMs = millis();
    StoredSatellites = InSatellites;
    bStoredSatellitesValid = bInSatellitesValid;
    StoredUtcString = InUtcString;

    if (bChanged)
    {
        bPersistDirty = true;
        LastPersistMarkMs = millis();
    }
}

