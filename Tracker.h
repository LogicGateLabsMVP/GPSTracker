#pragma once

#include "Config.h"
#include "Logger.h"
#include "Storage.h"
#include "DisplayModule.h"
#include "GPSModule.h"
#include "GSMModule.h"
#include "SpaceImpact.h"

class FTrackerDevice
{
public:
    FTrackerDevice();
    ~FTrackerDevice();

    bool Initialize();
    void Update();

private:
    static void StaticGsmProgressCallback(void* Context);
    void OnGsmProgress();

    void PersistGpsFixIfNeeded();
    void ResetPeriodicTimer();
    void HandleGsmAsyncResults();

    void UpdatePeriodicSmsScheduler();
    void StartPeriodicSmsCycle();

    void UpdateStatusScreenRefresh();

    void HandleButton(EButtonEvent Button);
    void HandleDiagnosticsButton(EButtonEvent Button);
    void HandleMenuButton(EButtonEvent Button);
    void HandlePeriodicSmsSettingsButton(EButtonEvent Button);
    void HandleEditButton(EButtonEvent Button);

    void BeginGpsDiagnostics();
    void BeginSpaceImpact();
    void BeginPeriodicSmsSettings();
    
    void ApplyPeriodicSmsSettings();
    void CancelPeriodicSmsSettings();
    
    void LogGsmSend(ELogLevel Level, const String& Message);
    void SendSmsNow();
    void RefreshStatusScreen();

    void MaybePowerDownGsmAfterInteractiveUse();
    void BeginRecipientEdit();

    int16_t GetMenuScrollOffset();
    uint8_t BuildWrappedLogLines(FLogger& SourceLogger, String* OutLines, uint8_t MaxOutLines);
    uint8_t GetWrappedLogLineCount();

    void Render();
    void RenderGpsDiagnostics();
    void RenderPeriodicSmsSettings();

    void BeginGpsBoundsMenu();
    int16_t GetGpsBoundsMenuScrollOffset();
    void BeginGpsBoundsEdit(uint8_t PointIndex);
    bool TryParseGpsBoundsEditBuffer(double& OutLatitude, double& OutLongitude);
    void ApplyGpsBoundsSettings();
    void CancelGpsBoundsSettings();
    void HandleGpsBoundsMenuButton(EButtonEvent Button);
    void HandleGpsBoundsEditButton(EButtonEvent Button);

    String BuildGpsBoundsPointLine(uint8_t PointIndex, const FGpsBoundPoint& Point);
    void RenderGpsBoundsMenu();
    void RenderGpsBoundsEdit();

    bool IsGpsPointWithinBounds(double Latitude, double Longitude);
    bool IsSmsBlockedByGpsBounds(String& OutReason);
    void RenderPrivateModeMenu();
    void RenderPasswordEntry();

    String MaskLiteral(const String& Value);
    String ReplaceAllExact(String Source, const String& Search, const String& Replacement);
    void MaskAfterLabel(String& Text, const String& Label);
    String ApplyPrivateMaskToText(const String& InText);
    
    void SanitizeLinesForDisplay(String* Lines, uint8_t NumLines);
    void ResetAllDataAndApplyDefaults();
    void BeginPrivateModeMenu();
    void BeginPasswordEntry(EPasswordPurpose InPurpose);
    void SubmitPasswordEntry();
    void HandlePrivateModeMenuButton(EButtonEvent Button);
    void HandlePasswordEntryButton(EButtonEvent Button);
    void AddDiag(const String& Line);

private:
    static constexpr uint8_t MenuCount = 10;
    static constexpr uint8_t MaxDiagLines = 16;
    static constexpr uint8_t MaxWrappedLogLines = 64;
    static constexpr uint8_t PrivatePasswordLength = 4;
    static constexpr uint8_t GpsBoundsMenuItemCount = 7;

    const char* MenuItems[MenuCount] =
    {
        "Send SMS now",
        "Periodical SMS settings",
        "Set recipient",
        "GPS bounds",
        "Private mode",
        "Device status",
        "Run diagnostics",
        "GPS diagnostics",
        "View logs",
        "Space Impact"
    };

    FLogger Logger;
    FLogger SendLogger;
    FConfigStorage Storage;
    FDisplayModule Display;
    FGPSModule Gps;
    FGSMModule Gsm;
    FSpaceImpact SpaceImpact;

    FDeviceConfig Config;
    FPeriodicSmsSettings PeriodicSettings;
    FPeriodicSmsSettings EditPeriodicSettings;
    FGpsBoundsSettings GpsBoundsSettings;
    FGpsBoundsSettings EditGpsBoundsSettings;
    FGsmStatus LastGsmStatus;

    EScreen CurrentScreen = EScreen::Diagnostics;
    bool bGpsInitOk = false;
    uint32_t GpsDiagStartMs = 0;
    uint32_t GpsDiagInitialChars = 0;

    String DiagLines[MaxDiagLines];
    uint8_t DiagCount = 0;
    int16_t DiagScroll = 0;

    uint8_t MenuIndex = 0;
    int16_t LogScroll = 0;

    char EditRecipientBuffer[20];
    uint8_t EditCursor = 1;
    char EditGpsBoundsBuffer[24] = "0.0000,0.0000";
    char EditPasswordBuffer[PrivatePasswordLength + 1] = {'0', '0', '0', '0', '\0'};
    uint8_t PasswordCursor = 0;
    uint8_t GpsBoundsSelection = 0;
    uint8_t GpsBoundsEditPointIndex = 0;
    uint8_t GpsBoundsEditCursor = 0;
    uint8_t PrivateModeSelection = 0;
    EPasswordPurpose PasswordPurpose = EPasswordPurpose::None;

    String BusyTitle;
    String BusyLineA;
    String BusyLineB;

    bool bShowGsmSendLogs = false;
    bool bManualSmsInProgress = false;
    bool bPeriodicSmsInProgress = false;
    bool bKeepGsmAliveForResend = false;
    bool bPowerDownWhenIdle = false;
    uint8_t PeriodicSettingsSelection = 0;
    bool bPeriodicEditingPeriod = false;
    uint32_t NextPeriodicSmsAtMs = 0;

    uint32_t LastRenderMs = 0;
    uint32_t LastStatusRefreshRequestMs = 0;
    uint32_t LastStatusRefreshVisualTickMs = 0;
};
