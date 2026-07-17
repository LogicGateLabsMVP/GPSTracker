#include "Tracker.h"

#include "Config.h"
#include "Logger.h"
#include "Storage.h"
#include "DisplayModule.h"
#include "GPSModule.h"
#include "GSMModule.h"
#include "SpaceImpact.h"

#include <string.h>
#include <cstdlib>

FTrackerDevice::FTrackerDevice()
    : Logger(true)
    , SendLogger(false)
    , Storage(Logger)
    , Gps(Logger)
    , Gsm(Logger)
    , SpaceImpact(Logger)
{
    memset(&Config, 0, sizeof(Config));
    strlcpy(Config.Recipient, AppConfig::DefaultRecipient, sizeof(Config.Recipient));
    memset(EditRecipientBuffer, 0, sizeof(EditRecipientBuffer));
}

FTrackerDevice::~FTrackerDevice() = default;

bool FTrackerDevice::Initialize()
{
    Serial.begin(AppConfig::UsbLogBaud);
    const uint32_t WaitStartMs = millis();
    while (!Serial && (millis() - WaitStartMs) < 200)
    {
        delay(5);
    }

    Logger.Log(ELogLevel::Info, "Tracker boot.");

    Display.Initialize();
    AddDiag("Display subsystem: OK");

    const bool bStorageOk = Storage.Initialize();
    AddDiag(String("Storage: ") + (bStorageOk ? "OK" : "FAIL"));
    if (bStorageOk)
    {
        Storage.Load(Config);
        Storage.LoadPeriodicSettings(PeriodicSettings);
        Storage.LoadGpsBoundsSettings(GpsBoundsSettings);
        EditGpsBoundsSettings = GpsBoundsSettings;

        FStoredGpsFixData PersistedFix;
        if (Storage.LoadStoredGpsFix(PersistedFix))
        {
            Gps.RestorePersistedFix(PersistedFix);
        }
    }

    bGpsInitOk = Gps.Initialize();
    AddDiag(String("GPS UART start: ") + (bGpsInitOk ? "OK" : "FAIL"));
    AddDiag(String("GPS fix: ") + (Gps.HasValidFix() ? "READY" : (Gps.HasStoredFix() ? "STORED FIX" : "NO FIX YET")));

    Gsm.BeginPowerControl();
    Gsm.PowerOff();
    AddDiag("GSM power: OFF");
    AddDiag(String("GSM BOOT GPIO") + String(HW::GsmBoot) + ": " + (Gsm.GetBootPinLevel() == HIGH ? "HIGH" : "LOW"));

    AddDiag("Recipient: " + String(Config.Recipient));
    AddDiag(String("Private mode: ") + (Config.bPrivateMode ? "ON" : "OFF"));
    AddDiag(String("GPS bounds: ") + (GpsBoundsSettings.bUseBounds ? "ON" : "OFF"));
    AddDiag(String("Periodic SMS: ") + (PeriodicSettings.bEnabled ? "ON" : "OFF") + ", " + String(PeriodicSettings.PeriodSeconds) + " s");

    if (PeriodicSettings.bEnabled)
    {
        ResetPeriodicTimer();
    }

    CurrentScreen = EScreen::Diagnostics;
    LastRenderMs = 0;
    return true;
}

void FTrackerDevice::Update()
{
    Gps.Update();
    PersistGpsFixIfNeeded();

    Gsm.Update();
    Display.Update();
    if (CurrentScreen == EScreen::SpaceImpact)
    {
        SpaceImpact.Update();
    }

    HandleGsmAsyncResults();

    if (Gsm.ConsumeStatusUpdate(LastGsmStatus))
    {
        LastStatusRefreshVisualTickMs = millis();
    }

    UpdateStatusScreenRefresh();
    UpdatePeriodicSmsScheduler();

    const EButtonEvent Button = Display.ConsumeButtonEvent();
    HandleButton(Button);

    if ((millis() - LastRenderMs) >= AppConfig::ScreenRefreshMs)
    {
        Render();
        LastRenderMs = millis();
    }
}

void FTrackerDevice::StaticGsmProgressCallback(void* Context)
{
    FTrackerDevice* Device = reinterpret_cast<FTrackerDevice*>(Context);
    if (Device != nullptr)
    {
        Device->OnGsmProgress();
    }
}

void FTrackerDevice::OnGsmProgress()
{
    if (CurrentScreen == EScreen::Logs && bShowGsmSendLogs)
    {
        Render();
    }
}

void FTrackerDevice::PersistGpsFixIfNeeded()
{
    FStoredGpsFixData PersistedFix;
    if (Gps.ConsumePersistableFix(PersistedFix))
    {
        Storage.SaveStoredGpsFix(PersistedFix);
    }
}

void FTrackerDevice::ResetPeriodicTimer()
{
    NextPeriodicSmsAtMs = millis() + PeriodicSettings.PeriodSeconds * 1000UL;
}

void FTrackerDevice::HandleGsmAsyncResults()
{
    bool bSendSuccess = false;
    if (Gsm.ConsumeSendSmsResult(bSendSuccess))
    {
        if (bManualSmsInProgress)
        {
            if (bSendSuccess)
            {
                bKeepGsmAliveForResend = false;
                LogGsmSend(ELogLevel::Info, "GSM final result: success.");
                if (PeriodicSettings.bEnabled)
                {
                    ResetPeriodicTimer();
                }
            }
            else
            {
                bKeepGsmAliveForResend = true;
                LogGsmSend(ELogLevel::Error, "GSM final result: failed. GSM stays initialized for resend.");
            }

            bManualSmsInProgress = false;
            Gsm.SetSessionLogger(nullptr);
            Gsm.SetProgressCallback(nullptr, nullptr);
            MaybePowerDownGsmAfterInteractiveUse();
        }
        else if (bPeriodicSmsInProgress)
        {
            if (bSendSuccess)
            {
                bKeepGsmAliveForResend = false;
                Logger.Log(ELogLevel::Info, "Periodic SMS sent.");
                ResetPeriodicTimer();
                Gsm.PowerOff();
            }
            else
            {
                bKeepGsmAliveForResend = true;
                Logger.Log(ELogLevel::Error, "Periodic SMS failed. GSM stays initialized for resend and will retry soon.");
                NextPeriodicSmsAtMs = millis() + AppConfig::GsmPeriodicRetryMs;
            }

            bPeriodicSmsInProgress = false;
        }
    }

    if (bPowerDownWhenIdle && !Gsm.IsBusy() && !bManualSmsInProgress && !bPeriodicSmsInProgress)
    {
        bPowerDownWhenIdle = false;
        if (!bKeepGsmAliveForResend)
        {
            Gsm.PowerOff();
        }
    }
}

void FTrackerDevice::UpdatePeriodicSmsScheduler()
{
    if (!PeriodicSettings.bEnabled || bPeriodicSmsInProgress || bManualSmsInProgress || Gsm.IsBusy())
    {
        return;
    }

    const uint32_t NowMs = millis();
    if (NowMs < NextPeriodicSmsAtMs)
    {
        return;
    }

    StartPeriodicSmsCycle();
}

void FTrackerDevice::StartPeriodicSmsCycle()
{
    if (strlen(Config.Recipient) == 0)
    {
        Logger.Log(ELogLevel::Error, "Periodic SMS aborted: recipient number is empty.");
        NextPeriodicSmsAtMs = millis() + AppConfig::GsmPeriodicRetryMs;
        return;
    }

    Logger.Log(ELogLevel::Info, "Periodic SMS cycle started.");

    String BoundsBlockReason;
    if (IsSmsBlockedByGpsBounds(BoundsBlockReason))
    {
        Logger.Log(ELogLevel::Warning, "Periodic SMS blocked: " + BoundsBlockReason);
        ResetPeriodicTimer();
        return;
    }

    const String SmsBody = Gps.BuildSmsBody();
    if (Gsm.StartSendSms(Config.Recipient, SmsBody))
    {
        bPeriodicSmsInProgress = true;
    }
    else
    {
        Logger.Log(ELogLevel::Warning, "Periodic SMS start deferred because GSM is busy.");
        NextPeriodicSmsAtMs = millis() + AppConfig::GsmPeriodicRetryMs;
    }
}

void FTrackerDevice::UpdateStatusScreenRefresh()
{
    if (CurrentScreen != EScreen::Status)
    {
        return;
    }

    LastGsmStatus = Gsm.GetUiStatusSnapshot();

    if (!Gsm.IsPoweredOn())
    {
        return;
    }

    const uint32_t NowMs = millis();
    if (Gsm.IsBusy())
    {
        return;
    }

    if ((NowMs - LastStatusRefreshRequestMs) >= AppConfig::GsmStatusRefreshMs)
    {
        LastStatusRefreshRequestMs = NowMs;
        Gsm.RequestStatusRefresh();
    }
}

void FTrackerDevice::HandleButton(EButtonEvent Button)
{
    switch (CurrentScreen)
    {
        case EScreen::Diagnostics:
            HandleDiagnosticsButton(Button);
            break;

        case EScreen::Menu:
            HandleMenuButton(Button);
            break;

        case EScreen::Status:
            if (Button == EButtonEvent::Back || Button == EButtonEvent::Ok)
            {
                CurrentScreen = EScreen::Menu;
                MaybePowerDownGsmAfterInteractiveUse();
            }
            break;

        case EScreen::Logs:
        {
            const uint8_t WrappedLogCount = GetWrappedLogLineCount();
            if (Button == EButtonEvent::Up && LogScroll > 0)
            {
                --LogScroll;
            }
            else if (Button == EButtonEvent::Down && (LogScroll + 6) < WrappedLogCount)
            {
                ++LogScroll;
            }
            else if ((Button == EButtonEvent::Back || Button == EButtonEvent::Ok) && !bManualSmsInProgress)
            {
                bShowGsmSendLogs = false;
                CurrentScreen = EScreen::Menu;
                MaybePowerDownGsmAfterInteractiveUse();
            }
            break;
        }

        case EScreen::GpsDiagnostics:
            if (Button == EButtonEvent::Ok)
            {
                BeginGpsDiagnostics();
            }
            else if (Button == EButtonEvent::Back)
            {
                CurrentScreen = EScreen::Menu;
            }
            break;

        case EScreen::EditRecipient:
            HandleEditButton(Button);
            break;

        case EScreen::PeriodicSmsSettings:
            HandlePeriodicSmsSettingsButton(Button);
            break;

        case EScreen::GpsBoundsMenu:
            HandleGpsBoundsMenuButton(Button);
            break;

        case EScreen::GpsBoundsEdit:
            HandleGpsBoundsEditButton(Button);
            break;

        case EScreen::PrivateModeMenu:
            HandlePrivateModeMenuButton(Button);
            break;

        case EScreen::PasswordEntry:
            HandlePasswordEntryButton(Button);
            break;

        case EScreen::SpaceImpact:
            if (Button == EButtonEvent::Back)
            {
                CurrentScreen = EScreen::Menu;
            }
            else
            {
                SpaceImpact.HandleButton(Button);
            }
            break;

        case EScreen::Message:
            if (Button == EButtonEvent::Back || Button == EButtonEvent::Ok)
            {
                CurrentScreen = EScreen::Menu;
            }
            break;
    }
}

void FTrackerDevice::HandleDiagnosticsButton(EButtonEvent Button)
{
    if (Button == EButtonEvent::Up && DiagScroll > 0)
    {
        --DiagScroll;
    }
    else if (Button == EButtonEvent::Down && (DiagScroll + 6) < DiagCount)
    {
        ++DiagScroll;
    }
    else if (Button == EButtonEvent::Ok)
    {
        CurrentScreen = EScreen::Menu;
    }
    else if (Button == EButtonEvent::Back)
    {
        DiagCount = 0;
        AddDiag("Re-run diagnostics");
        AddDiag("GPS: " + Gps.GetShortStatus());
        AddDiag("Recipient: " + String(Config.Recipient));
        AddDiag(String("Private mode: ") + (Config.bPrivateMode ? "ON" : "OFF"));
        AddDiag(String("GPS bounds: ") + (GpsBoundsSettings.bUseBounds ? "ON" : "OFF"));
        AddDiag(String("Periodic SMS: ") + (PeriodicSettings.bEnabled ? "ON" : "OFF") + ", " + String(PeriodicSettings.PeriodSeconds) + " s");
        AddDiag(String("GSM BOOT GPIO") + String(HW::GsmBoot) + ": " + (Gsm.GetBootPinLevel() == HIGH ? "HIGH" : "LOW") + (Gsm.IsBootDrivingGround() ? " GND" : " REL"));
    }
}

void FTrackerDevice::HandleMenuButton(EButtonEvent Button)
{
    if (Button == EButtonEvent::Up)
    {
        MenuIndex = (MenuIndex == 0) ? (MenuCount - 1) : (MenuIndex - 1);
    }
    else if (Button == EButtonEvent::Down)
    {
        MenuIndex = (MenuIndex + 1) % MenuCount;
    }
    else if (Button == EButtonEvent::Ok)
    {
        switch (MenuIndex)
        {
            case 0: SendSmsNow(); break;
            case 1: BeginPeriodicSmsSettings(); break;
            case 2: BeginRecipientEdit(); break;
            case 3: BeginGpsBoundsMenu(); break;
            case 4: BeginPrivateModeMenu(); break;
            case 5: RefreshStatusScreen(); break;
            case 6: CurrentScreen = EScreen::Diagnostics; break;
            case 7: BeginGpsDiagnostics(); break;
            case 8: bShowGsmSendLogs = false; CurrentScreen = EScreen::Logs; break;
            case 9: BeginSpaceImpact(); break;
            default: break;
        }
    }
}

void FTrackerDevice::BeginGpsDiagnostics()
{
    GpsDiagStartMs = millis();
    GpsDiagInitialChars = Gps.GetCharsProcessed();
    CurrentScreen = EScreen::GpsDiagnostics;
    Logger.Log(ELogLevel::Info, "GPS diagnostics started.");
}

void FTrackerDevice::BeginSpaceImpact()
{
    SpaceImpact.Start();
    CurrentScreen = EScreen::SpaceImpact;
}

void FTrackerDevice::BeginPeriodicSmsSettings()
{
    EditPeriodicSettings = PeriodicSettings;
    PeriodicSettingsSelection = 0;
    bPeriodicEditingPeriod = false;
    CurrentScreen = EScreen::PeriodicSmsSettings;
}

void FTrackerDevice::HandlePeriodicSmsSettingsButton(EButtonEvent Button)
{
    if (bPeriodicEditingPeriod)
    {
        if (Button == EButtonEvent::Up)
        {
            if (EditPeriodicSettings.PeriodSeconds < AppConfig::MaxPeriodicSmsSeconds)
            {
                EditPeriodicSettings.PeriodSeconds += AppConfig::PeriodicSmsStepSeconds;
                if (EditPeriodicSettings.PeriodSeconds > AppConfig::MaxPeriodicSmsSeconds)
                {
                    EditPeriodicSettings.PeriodSeconds = AppConfig::MaxPeriodicSmsSeconds;
                }
            }
        }
        else if (Button == EButtonEvent::Down)
        {
            if (EditPeriodicSettings.PeriodSeconds > AppConfig::MinPeriodicSmsSeconds)
            {
                const uint32_t Delta = AppConfig::PeriodicSmsStepSeconds;
                EditPeriodicSettings.PeriodSeconds = (EditPeriodicSettings.PeriodSeconds > Delta) ? (EditPeriodicSettings.PeriodSeconds - Delta) : AppConfig::MinPeriodicSmsSeconds;
                if (EditPeriodicSettings.PeriodSeconds < AppConfig::MinPeriodicSmsSeconds)
                {
                    EditPeriodicSettings.PeriodSeconds = AppConfig::MinPeriodicSmsSeconds;
                }
            }
        }
        else if (Button == EButtonEvent::Ok || Button == EButtonEvent::Back)
        {
            bPeriodicEditingPeriod = false;
        }
        return;
    }

    if (Button == EButtonEvent::Up)
    {
        PeriodicSettingsSelection = (PeriodicSettingsSelection == 0) ? 3 : (PeriodicSettingsSelection - 1);
    }
    else if (Button == EButtonEvent::Down)
    {
        PeriodicSettingsSelection = (PeriodicSettingsSelection + 1) % 4;
    }
    else if (Button == EButtonEvent::Ok)
    {
        switch (PeriodicSettingsSelection)
        {
            case 0:
                EditPeriodicSettings.bEnabled = !EditPeriodicSettings.bEnabled;
                break;
            case 1:
                bPeriodicEditingPeriod = true;
                break;
            case 2:
                ApplyPeriodicSmsSettings();
                break;
            case 3:
                CancelPeriodicSmsSettings();
                break;
            default:
                break;
        }
    }
    else if (Button == EButtonEvent::Back)
    {
        CancelPeriodicSmsSettings();
    }
}

void FTrackerDevice::ApplyPeriodicSmsSettings()
{
    PeriodicSettings = EditPeriodicSettings;
    Storage.SavePeriodicSettings(PeriodicSettings);

    if (PeriodicSettings.bEnabled)
    {
        ResetPeriodicTimer();
    }
    else
    {
        NextPeriodicSmsAtMs = 0;
        bKeepGsmAliveForResend = false;
        if (CurrentScreen != EScreen::Status)
        {
            Gsm.PowerOff();
        }
    }

    BusyTitle = "Periodic SMS";
    BusyLineA = String("State: ") + (PeriodicSettings.bEnabled ? "ON" : "OFF");
    BusyLineB = String("Period: ") + String(PeriodicSettings.PeriodSeconds) + " s";
    CurrentScreen = EScreen::Message;
}

void FTrackerDevice::CancelPeriodicSmsSettings()
{
    EditPeriodicSettings = PeriodicSettings;
    bPeriodicEditingPeriod = false;
    CurrentScreen = EScreen::Menu;
}

void FTrackerDevice::HandleEditButton(EButtonEvent Button)
{
    static constexpr char AllowedChars[] = "+0123456789";

    if (Button == EButtonEvent::Up || Button == EButtonEvent::Down)
    {
        if (EditCursor == 0)
        {
            EditRecipientBuffer[0] = '+';
            return;
        }

        char& CurrentChar = EditRecipientBuffer[EditCursor];
        const char* CurrentPos = strchr(AllowedChars, CurrentChar);
        int32_t Index = CurrentPos ? static_cast<int32_t>(CurrentPos - AllowedChars) : 1;

        if (Button == EButtonEvent::Up)
        {
            ++Index;
        }
        else
        {
            --Index;
        }

        if (Index < 1)
        {
            Index = 10;
        }
        if (Index > 10)
        {
            Index = 1;
        }

        CurrentChar = AllowedChars[Index];
    }
    else if (Button == EButtonEvent::Ok)
    {
        if (EditCursor + 1 < strlen(EditRecipientBuffer))
        {
            ++EditCursor;
        }
        else
        {
            strlcpy(Config.Recipient, EditRecipientBuffer, sizeof(Config.Recipient));
            Storage.Save(Config);
            BusyTitle = "Saved";
            BusyLineA = "Recipient stored.";
            BusyLineB = String(Config.Recipient);
            CurrentScreen = EScreen::Message;
        }
    }
    else if (Button == EButtonEvent::Back)
    {
        if (EditCursor > 1)
        {
            --EditCursor;
        }
        else
        {
            CurrentScreen = EScreen::Menu;
        }
    }
}

void FTrackerDevice::LogGsmSend(ELogLevel Level, const String& Message)
{
    SendLogger.Log(Level, Message);
    Logger.Log(Level, Message);
}

void FTrackerDevice::SendSmsNow()
{
    if (bManualSmsInProgress || bPeriodicSmsInProgress || Gsm.IsBusy())
    {
        BusyTitle = "GSM busy";
        BusyLineA = "Operation in progress.";
        BusyLineB = "Please wait.";
        CurrentScreen = EScreen::Message;
        return;
    }

    String BoundsBlockReason;
    if (IsSmsBlockedByGpsBounds(BoundsBlockReason))
    {
        Logger.Log(ELogLevel::Warning, "Manual SMS blocked: " + BoundsBlockReason);
        BusyTitle = "SMS blocked";
        BusyLineA = BoundsBlockReason;
        BusyLineB = "Not sending";
        CurrentScreen = EScreen::Message;
        return;
    }

    bShowGsmSendLogs = true;
    SendLogger.Clear();
    CurrentScreen = EScreen::Logs;
    LogScroll = 0;

    Gsm.SetSessionLogger(&SendLogger);
    Gsm.SetProgressCallback(&FTrackerDevice::StaticGsmProgressCallback, this);

    if (strlen(Config.Recipient) == 0)
    {
        LogGsmSend(ELogLevel::Error, "GSM send aborted: recipient number is empty.");
        LogGsmSend(ELogLevel::Error, "Open Set recipient and try again.");
        Gsm.SetSessionLogger(nullptr);
        Gsm.SetProgressCallback(nullptr, nullptr);
        Render();
        return;
    }

    const bool bHasCurrentFix = Gps.HasValidFix();
    const bool bHasStoredFix = Gps.HasStoredFix();
    const String SmsBody = Gps.BuildSmsBody();

    LogGsmSend(ELogLevel::Info, "GSM session started.");
    LogGsmSend(ELogLevel::Info, "Recipient: " + String(Config.Recipient));

    if (SmsBody == "No GPS data")
    {
        LogGsmSend(ELogLevel::Warning, "Payload source: no GPS data.");
    }
    else if (!bHasCurrentFix && bHasStoredFix)
    {
        LogGsmSend(ELogLevel::Warning, "Payload source: last known GPS fix.");
    }
    else
    {
        LogGsmSend(ELogLevel::Info, "Payload source: fresh GPS fix.");
    }

    LogGsmSend(ELogLevel::Info, "Entering GSM SMS send flow.");
    if (Gsm.StartSendSms(Config.Recipient, SmsBody))
    {
        bManualSmsInProgress = true;
    }
    else
    {
        LogGsmSend(ELogLevel::Error, "Could not start GSM SMS flow.");
        Gsm.SetSessionLogger(nullptr);
        Gsm.SetProgressCallback(nullptr, nullptr);
    }
    Render();
}

void FTrackerDevice::RefreshStatusScreen()
{
    CurrentScreen = EScreen::Status;
    LastStatusRefreshRequestMs = 0;
    LastGsmStatus = Gsm.GetUiStatusSnapshot();
    if (Gsm.IsPoweredOn() && !Gsm.IsBusy())
    {
        Gsm.RequestStatusRefresh();
    }
}

void FTrackerDevice::MaybePowerDownGsmAfterInteractiveUse()
{
    if (bKeepGsmAliveForResend)
    {
        return;
    }

    if (CurrentScreen == EScreen::Status || bManualSmsInProgress || bPeriodicSmsInProgress)
    {
        bPowerDownWhenIdle = true;
        return;
    }

    if (Gsm.IsBusy())
    {
        bPowerDownWhenIdle = true;
        return;
    }

    Gsm.PowerOff();
}

void FTrackerDevice::BeginRecipientEdit()
{
    strlcpy(EditRecipientBuffer, Config.Recipient, sizeof(EditRecipientBuffer));
    if (strlen(EditRecipientBuffer) == 0)
    {
        strlcpy(EditRecipientBuffer, AppConfig::DefaultRecipient, sizeof(EditRecipientBuffer));
    }

    EditRecipientBuffer[0] = '+';
    EditCursor = 1;
    CurrentScreen = EScreen::EditRecipient;
}

int16_t FTrackerDevice::GetMenuScrollOffset()
{
    static constexpr int16_t VisibleMenuRows = 6;
    if (MenuCount <= VisibleMenuRows)
    {
        return 0;
    }
    if (MenuIndex < VisibleMenuRows)
    {
        return 0;
    }
    const int16_t MaxScrollOffset = static_cast<int16_t>(MenuCount) - VisibleMenuRows;
    int16_t ScrollOffset = static_cast<int16_t>(MenuIndex) - (VisibleMenuRows - 1);
    if (ScrollOffset > MaxScrollOffset)
    {
        ScrollOffset = MaxScrollOffset;
    }
    return ScrollOffset;
}

uint8_t FTrackerDevice::BuildWrappedLogLines(FLogger& SourceLogger, String* OutLines, uint8_t MaxOutLines)
{
    static constexpr int16_t WrapWidthChars = 21;
    const uint8_t LogCount = SourceLogger.GetCount();
    uint8_t OutCount = 0;

    for (uint8_t LogIndex = 0; LogIndex < LogCount && OutCount < MaxOutLines; ++LogIndex)
    {
        const String FullLine = ApplyPrivateMaskToText(SourceLogger.GetNewest(LogIndex));
        if (FullLine.length() == 0)
        {
            OutLines[OutCount++] = "";
            if (OutCount < MaxOutLines)
            {
                OutLines[OutCount++] = "";
            }
            continue;
        }

        int32_t Start = 0;
        while (Start < FullLine.length() && OutCount < MaxOutLines)
        {
            int32_t End = Start + WrapWidthChars;
            if (End >= FullLine.length())
            {
                OutLines[OutCount++] = FullLine.substring(Start);
                break;
            }

            int32_t Split = End;
            for (int32_t Index = End; Index > Start; --Index)
            {
                if (FullLine[Index] == ' ')
                {
                    Split = Index;
                    break;
                }
            }

            if (Split <= Start)
            {
                Split = End;
            }

            OutLines[OutCount++] = FullLine.substring(Start, Split);
            Start = Split;
            while (Start < FullLine.length() && FullLine[Start] == ' ')
            {
                ++Start;
            }
        }

        if (OutCount < MaxOutLines)
        {
            OutLines[OutCount++] = "";
        }
    }

    return OutCount;
}

uint8_t FTrackerDevice::GetWrappedLogLineCount()
{
    String WrappedLines[MaxWrappedLogLines];
    FLogger& ActiveLogger = bShowGsmSendLogs ? SendLogger : Logger;
    return BuildWrappedLogLines(ActiveLogger, WrappedLines, MaxWrappedLogLines);
}

void FTrackerDevice::Render()
{
    switch (CurrentScreen)
    {
        case EScreen::Diagnostics:
        {
            String Lines[MaxDiagLines];
            for (uint8_t Index = 0; Index < DiagCount; ++Index)
            {
                Lines[Index] = ApplyPrivateMaskToText(DiagLines[Index]);
            }
            Display.DrawLines("Diagnostics", Lines, DiagCount, DiagScroll, -1);
            break;
        }

        case EScreen::Menu:
        {
            String MenuLines[MenuCount];
            for (uint8_t Index = 0; Index < MenuCount; ++Index)
            {
                MenuLines[Index] = MenuItems[Index];
            }
            Display.DrawLines("Main menu", MenuLines, MenuCount, GetMenuScrollOffset(), MenuIndex);
            break;
        }

                                case EScreen::Status:
        {
            const FGsmStatus StatusView = Gsm.GetUiStatusSnapshot();
            const uint32_t SpinnerAgeMs = millis() - LastStatusRefreshVisualTickMs;
            const char SpinnerFrames[] = {'|', '/', '-', '\\'};
            const char SpinnerChar = SpinnerFrames[(SpinnerAgeMs / 250) % 4];
            const String Title = Gsm.IsBusy() ? String("Device status ") + SpinnerChar : "Device status";

            String Lines[6];
            Lines[0] = "Phone: " + String(Config.Recipient);
            Lines[1] = "GPS: " + Gps.GetShortStatus();
            Lines[2] = "SIM: " + StatusView.SimState;
            Lines[3] = "NET: " + StatusView.NetworkState;
            if (!Gsm.IsPoweredOn())
            {
                Lines[4] = "CSQ: GSM disabled";
                Lines[5] = "OP: GSM disabled";
            }
            else if (StatusView.Csq >= 0 && StatusView.Csq <= 31)
            {
                const int32_t Dbm = -113 + 2 * StatusView.Csq;
                Lines[4] = "CSQ: " + String(StatusView.Csq) + " / " + String(Dbm) + " dBm";
                Lines[5] = "OP: " + StatusView.OperatorName;
            }
            else
            {
                Lines[4] = Gsm.IsBusy() ? "CSQ: updating" : "CSQ: unknown";
                Lines[5] = "OP: " + StatusView.OperatorName;
            }
            SanitizeLinesForDisplay(Lines, 6);
            Display.DrawLines(Title, Lines, 6, 0, -1);
            break;
        }

case EScreen::Logs:
        {
            String WrappedLogLines[MaxWrappedLogLines];
            FLogger& ActiveLogger = bShowGsmSendLogs ? SendLogger : Logger;
            const uint8_t WrappedCount = BuildWrappedLogLines(ActiveLogger, WrappedLogLines, MaxWrappedLogLines);
            Display.DrawLines(bManualSmsInProgress ? "GSM sending" : (bShowGsmSendLogs ? "GSM send logs" : "Logs"), WrappedLogLines, WrappedCount, LogScroll, -1);
            break;
        }

        case EScreen::GpsDiagnostics:
            RenderGpsDiagnostics();
            break;

        case EScreen::EditRecipient:
        {
            if (Config.bPrivateMode)
            {
                String MaskedBuffer = MaskLiteral(String(EditRecipientBuffer));
                Display.DrawRecipientEditor(MaskedBuffer.c_str(), EditCursor);
            }
            else
            {
                Display.DrawRecipientEditor(EditRecipientBuffer, EditCursor);
            }
            break;
        }

        case EScreen::PeriodicSmsSettings:
            RenderPeriodicSmsSettings();
            break;

        case EScreen::GpsBoundsMenu:
            RenderGpsBoundsMenu();
            break;

        case EScreen::GpsBoundsEdit:
            RenderGpsBoundsEdit();
            break;

        case EScreen::PrivateModeMenu:
            RenderPrivateModeMenu();
            break;

        case EScreen::PasswordEntry:
            RenderPasswordEntry();
            break;

        case EScreen::SpaceImpact:
            SpaceImpact.Render(Display);
            break;

        case EScreen::Message:
        {
            const String LineA = ApplyPrivateMaskToText(BusyLineA);
            const String LineB = ApplyPrivateMaskToText(BusyLineB);
            Display.DrawMessage(BusyTitle, LineA, LineB, "OK/Back = menu");
            break;
        }
    }
}

void FTrackerDevice::RenderGpsDiagnostics()
{
    String Lines[6];
    const uint32_t ElapsedMs = millis() - GpsDiagStartMs;
    const uint32_t CharsNow = Gps.GetCharsProcessed();
    const uint32_t NewChars = (CharsNow >= GpsDiagInitialChars) ? (CharsNow - GpsDiagInitialChars) : 0;

    Lines[0] = String("UART: ") + (Gps.IsUartStarted() ? "OK" : "FAIL");
    if (NewChars > 10)
    {
        Lines[1] = "Traffic: DATA (" + String(NewChars) + " ch)";
    }
    else if (ElapsedMs < AppConfig::GpsWarmupProbeMs)
    {
        Lines[1] = "Traffic: wait " + FormatSeconds(AppConfig::GpsWarmupProbeMs - ElapsedMs, 1) + " s";
    }
    else
    {
        Lines[1] = "Traffic: NO DATA";
    }

    if (Gps.HasValidFix())
    {
        Lines[2] = "Fix: OK " + Gps.GetFixSummary();
    }
    else if (NewChars <= 10)
    {
        Lines[2] = "Fix: waiting NMEA";
    }
    else if (ElapsedMs < AppConfig::GpsDiagnosticsFixWaitMs)
    {
        Lines[2] = "Fix: waiting " + FormatSeconds(AppConfig::GpsDiagnosticsFixWaitMs - ElapsedMs, 1) + " s";
    }
    else
    {
        Lines[2] = "Fix: NO FIX";
    }

    Lines[3] = "Lat: " + Gps.GetLatitudeString(6);
    Lines[4] = "Lng: " + Gps.GetLongitudeString(6);
    Lines[5] = "Sat: " + Gps.GetSatellitesString() + " OK=R";
    SanitizeLinesForDisplay(Lines, 6);
    Display.DrawLines("GPS diagnostics", Lines, 6, 0, -1);
}

void FTrackerDevice::RenderPeriodicSmsSettings()
{
    String Lines[6];
    Lines[0] = "Enabled: " + String(EditPeriodicSettings.bEnabled ? "ON" : "OFF");
    Lines[1] = "Period: " + String(EditPeriodicSettings.PeriodSeconds) + " s" + (bPeriodicEditingPeriod ? " *" : "");
    Lines[2] = "Apply";
    Lines[3] = "Cancel";
    Lines[4] = "OK=tgl/edit";
    Lines[5] = "Back=cancel";
    Display.DrawLines("Periodical SMS", Lines, 6, 0, bPeriodicEditingPeriod ? 1 : PeriodicSettingsSelection);
}

void FTrackerDevice::BeginGpsBoundsMenu()
{
    EditGpsBoundsSettings = GpsBoundsSettings;
    GpsBoundsSelection = 0;
    CurrentScreen = EScreen::GpsBoundsMenu;
}

int16_t FTrackerDevice::GetGpsBoundsMenuScrollOffset()
{
    static constexpr int16_t VisibleRows = 6;
    if (GpsBoundsMenuItemCount <= VisibleRows)
    {
        return 0;
    }
    if (GpsBoundsSelection < VisibleRows)
    {
        return 0;
    }

    const int16_t MaxScrollOffset = static_cast<int16_t>(GpsBoundsMenuItemCount) - VisibleRows;
    int16_t ScrollOffset = static_cast<int16_t>(GpsBoundsSelection) - (VisibleRows - 1);
    if (ScrollOffset > MaxScrollOffset)
    {
        ScrollOffset = MaxScrollOffset;
    }
    return ScrollOffset;
}

void FTrackerDevice::BeginGpsBoundsEdit(uint8_t PointIndex)
{
    GpsBoundsEditPointIndex = PointIndex;
    snprintf(
        EditGpsBoundsBuffer,
        sizeof(EditGpsBoundsBuffer),
        "%.4f,%.4f",
        EditGpsBoundsSettings.Points[PointIndex].Latitude,
        EditGpsBoundsSettings.Points[PointIndex].Longitude
    );
    GpsBoundsEditCursor = 0;
    CurrentScreen = EScreen::GpsBoundsEdit;
}

bool FTrackerDevice::TryParseGpsBoundsEditBuffer(double& OutLatitude, double& OutLongitude)
{
    const String RawText = String(EditGpsBoundsBuffer);
    const int32_t CommaIndex = RawText.indexOf(',');
    if (CommaIndex <= 0 || CommaIndex >= static_cast<int32_t>(RawText.length()) - 1)
    {
        return false;
    }

    String LatText = RawText.substring(0, CommaIndex);
    String LngText = RawText.substring(CommaIndex + 1);
    LatText.trim();
    LngText.trim();
    if (LatText.length() == 0 || LngText.length() == 0)
    {
        return false;
    }

    char* LatEnd = nullptr;
    char* LngEnd = nullptr;
    const double Latitude = strtod(LatText.c_str(), &LatEnd);
    const double Longitude = strtod(LngText.c_str(), &LngEnd);
    if (LatEnd == LatText.c_str() || LngEnd == LngText.c_str())
    {
        return false;
    }
    while (*LatEnd == ' ')
    {
        ++LatEnd;
    }
    while (*LngEnd == ' ')
    {
        ++LngEnd;
    }
    if (*LatEnd != '\0' || *LngEnd != '\0')
    {
        return false;
    }
    if (Latitude < -90.0 || Latitude > 90.0 || Longitude < -180.0 || Longitude > 180.0)
    {
        return false;
    }

    OutLatitude = Latitude;
    OutLongitude = Longitude;
    return true;
}

void FTrackerDevice::ApplyGpsBoundsSettings()
{
    GpsBoundsSettings = EditGpsBoundsSettings;
    Storage.SaveGpsBoundsSettings(GpsBoundsSettings);
    BusyTitle = "GPS bounds";
    BusyLineA = String("State: ") + (GpsBoundsSettings.bUseBounds ? "ON" : "OFF");
    BusyLineB = "Bounds saved";
    CurrentScreen = EScreen::Message;
}

void FTrackerDevice::CancelGpsBoundsSettings()
{
    EditGpsBoundsSettings = GpsBoundsSettings;
    CurrentScreen = EScreen::Menu;
}

void FTrackerDevice::HandleGpsBoundsMenuButton(EButtonEvent Button)
{
    if (Button == EButtonEvent::Up)
    {
        GpsBoundsSelection = (GpsBoundsSelection == 0) ? (GpsBoundsMenuItemCount - 1) : (GpsBoundsSelection - 1);
    }
    else if (Button == EButtonEvent::Down)
    {
        GpsBoundsSelection = (GpsBoundsSelection + 1) % GpsBoundsMenuItemCount;
    }
    else if (Button == EButtonEvent::Back)
    {
        CancelGpsBoundsSettings();
    }
    else if (Button == EButtonEvent::Ok)
    {
        if (GpsBoundsSelection == 0)
        {
            EditGpsBoundsSettings.bUseBounds = !EditGpsBoundsSettings.bUseBounds;
        }
        else if (GpsBoundsSelection >= 1 && GpsBoundsSelection <= 4)
        {
            BeginGpsBoundsEdit(GpsBoundsSelection - 1);
        }
        else if (GpsBoundsSelection == 5)
        {
            ApplyGpsBoundsSettings();
        }
        else
        {
            CancelGpsBoundsSettings();
        }
    }
}

void FTrackerDevice::HandleGpsBoundsEditButton(EButtonEvent Button)
{
    static constexpr char AllowedChars[] = "-0123456789.,";

    if (Button == EButtonEvent::Up || Button == EButtonEvent::Down)
    {
        char& CurrentChar = EditGpsBoundsBuffer[GpsBoundsEditCursor];
        const char* CurrentPos = strchr(AllowedChars, CurrentChar);
        int32_t CharIndex = CurrentPos ? static_cast<int32_t>(CurrentPos - AllowedChars) : 0;
        CharIndex += (Button == EButtonEvent::Up) ? 1 : -1;
        const int32_t MaxIndex = static_cast<int32_t>(strlen(AllowedChars)) - 1;
        if (CharIndex < 0)
        {
            CharIndex = MaxIndex;
        }
        if (CharIndex > MaxIndex)
        {
            CharIndex = 0;
        }
        CurrentChar = AllowedChars[CharIndex];
    }
    else if (Button == EButtonEvent::Ok)
    {
        if (GpsBoundsEditCursor + 1 < strlen(EditGpsBoundsBuffer))
        {
            ++GpsBoundsEditCursor;
        }
        else
        {
            double Latitude = 0.0;
            double Longitude = 0.0;
            if (TryParseGpsBoundsEditBuffer(Latitude, Longitude))
            {
                EditGpsBoundsSettings.Points[GpsBoundsEditPointIndex].Latitude = Latitude;
                EditGpsBoundsSettings.Points[GpsBoundsEditPointIndex].Longitude = Longitude;
                CurrentScreen = EScreen::GpsBoundsMenu;
            }
            else
            {
                BusyTitle = "GPS bounds";
                BusyLineA = "Invalid point";
                BusyLineB = "Use lat,lng";
                CurrentScreen = EScreen::Message;
            }
        }
    }
    else if (Button == EButtonEvent::Back)
    {
        if (GpsBoundsEditCursor > 0)
        {
            --GpsBoundsEditCursor;
        }
        else
        {
            CurrentScreen = EScreen::GpsBoundsMenu;
        }
    }
}

String FTrackerDevice::BuildGpsBoundsPointLine(uint8_t PointIndex, const FGpsBoundPoint& Point)
{
    return String("P") + String(PointIndex + 1) + ":" + FormatCoordinate(Point.Latitude, 4) + "," + FormatCoordinate(Point.Longitude, 4);
}

void FTrackerDevice::RenderGpsBoundsMenu()
{
    String Lines[GpsBoundsMenuItemCount];
    Lines[0] = "Use bounds: " + String(EditGpsBoundsSettings.bUseBounds ? "ON" : "OFF");
    for (uint8_t Index = 0; Index < 4; ++Index)
    {
        Lines[Index + 1] = BuildGpsBoundsPointLine(Index, EditGpsBoundsSettings.Points[Index]);
    }
    Lines[5] = "Apply";
    Lines[6] = "Cancel";
    SanitizeLinesForDisplay(Lines, GpsBoundsMenuItemCount);
    Display.DrawLines("GPS bounds", Lines, GpsBoundsMenuItemCount, GetGpsBoundsMenuScrollOffset(), GpsBoundsSelection);
}

void FTrackerDevice::RenderGpsBoundsEdit()
{
    if (Config.bPrivateMode)
    {
        String MaskedBuffer = MaskLiteral(String(EditGpsBoundsBuffer));
        Display.DrawTextEditor(
            String("Bound P") + String(GpsBoundsEditPointIndex + 1),
            MaskedBuffer.c_str(),
            GpsBoundsEditCursor,
            "Up/Down = char",
            "OK = next/save",
            "Back = prev/cancel"
        );
        return;
    }

    Display.DrawTextEditor(
        String("Bound P") + String(GpsBoundsEditPointIndex + 1),
        EditGpsBoundsBuffer,
        GpsBoundsEditCursor,
        "Up/Down = char",
        "OK = next/save",
        "Back = prev/cancel"
    );
}

bool FTrackerDevice::IsGpsPointWithinBounds(double Latitude, double Longitude)
{
    if (!GpsBoundsSettings.bUseBounds)
    {
        return true;
    }

    double MinLatitude = GpsBoundsSettings.Points[0].Latitude;
    double MaxLatitude = GpsBoundsSettings.Points[0].Latitude;
    double MinLongitude = GpsBoundsSettings.Points[0].Longitude;
    double MaxLongitude = GpsBoundsSettings.Points[0].Longitude;

    for (uint8_t Index = 1; Index < 4; ++Index)
    {
        if (GpsBoundsSettings.Points[Index].Latitude < MinLatitude)
        {
            MinLatitude = GpsBoundsSettings.Points[Index].Latitude;
        }
        if (GpsBoundsSettings.Points[Index].Latitude > MaxLatitude)
        {
            MaxLatitude = GpsBoundsSettings.Points[Index].Latitude;
        }
        if (GpsBoundsSettings.Points[Index].Longitude < MinLongitude)
        {
            MinLongitude = GpsBoundsSettings.Points[Index].Longitude;
        }
        if (GpsBoundsSettings.Points[Index].Longitude > MaxLongitude)
        {
            MaxLongitude = GpsBoundsSettings.Points[Index].Longitude;
        }
    }

    return Latitude >= MinLatitude && Latitude <= MaxLatitude && Longitude >= MinLongitude && Longitude <= MaxLongitude;
}

bool FTrackerDevice::IsSmsBlockedByGpsBounds(String& OutReason)
{
    if (!GpsBoundsSettings.bUseBounds)
    {
        return false;
    }

    double Latitude = 0.0;
    double Longitude = 0.0;
    if (!Gps.GetSmsCandidateCoordinates(Latitude, Longitude))
    {
        return false;
    }

    if (IsGpsPointWithinBounds(Latitude, Longitude))
    {
        return false;
    }

    OutReason = "GPS out of bounds";
    return true;
}

void FTrackerDevice::RenderPrivateModeMenu()
{
    if (!Config.bPrivateMode)
    {
        String Lines[6];
        Lines[0] = "State: OFF";
        Lines[1] = "Enable private mode";
        Lines[2] = "Back";
        Lines[3] = "";
        Lines[4] = "Hide phone & GPS";
        Lines[5] = "OK = select";
        Display.DrawLines("Private mode", Lines, 6, 0, PrivateModeSelection + 1);
        return;
    }

    String Lines[6];
    Lines[0] = "State: ON";
    Lines[1] = "Disable: password";
    Lines[2] = "Reset all data";
    Lines[3] = "Back";
    Lines[4] = "Hidden on screen";
    Lines[5] = "OK = select";
    Display.DrawLines("Private mode", Lines, 6, 0, PrivateModeSelection + 1);
}

void FTrackerDevice::RenderPasswordEntry()
{
    const String Title = (PasswordPurpose == EPasswordPurpose::EnablePrivateMode) ? "Set password" : "Enter password";
    Display.DrawPasswordEditor(Title, EditPasswordBuffer, PasswordCursor);
}

String FTrackerDevice::MaskLiteral(const String& Value)
{
    String Result;
    for (uint16_t Index = 0; Index < Value.length(); ++Index)
    {
        const char Character = Value[Index];
        Result += (Character == ' ' ? ' ' : '*');
    }
    return Result;
}

String FTrackerDevice::ReplaceAllExact(String Source, const String& Search, const String& Replacement)
{
    if (Search.length() == 0)
    {
        return Source;
    }

    Source.replace(Search, Replacement);
    return Source;
}

void FTrackerDevice::MaskAfterLabel(String& Text, const String& Label)
{
    const int32_t LabelIndex = Text.indexOf(Label);
    if (LabelIndex < 0)
    {
        return;
    }

    const int32_t Start = LabelIndex + Label.length();
    int32_t End = Text.indexOf('\n', Start);
    if (End < 0)
    {
        End = Text.length();
    }

    for (int32_t Index = Start; Index < End; ++Index)
    {
        if (Text[Index] != ' ')
        {
            Text.setCharAt(Index, '*');
        }
    }
}

String FTrackerDevice::ApplyPrivateMaskToText(const String& InText)
{
    if (!Config.bPrivateMode)
    {
        return InText;
    }

    String OutText = InText;
    const String RecipientString = String(Config.Recipient);
    if (RecipientString.length() > 0)
    {
        OutText = ReplaceAllExact(OutText, RecipientString, MaskLiteral(RecipientString));
    }

    MaskAfterLabel(OutText, "Phone: ");
    MaskAfterLabel(OutText, "Recipient: ");
    MaskAfterLabel(OutText, "Lat: ");
    MaskAfterLabel(OutText, "Lng: ");
    MaskAfterLabel(OutText, "P1:");
    MaskAfterLabel(OutText, "P2:");
    MaskAfterLabel(OutText, "P3:");
    MaskAfterLabel(OutText, "P4:");
    MaskAfterLabel(OutText, "Fix OK: ");
    MaskAfterLabel(OutText, "?q=");

    if (OutText.startsWith("GPS: ") && OutText.indexOf("Fix OK: ") < 0 && OutText.indexOf("No GPS") < 0)
    {
        MaskAfterLabel(OutText, "GPS: ");
    }

    return OutText;
}

void FTrackerDevice::SanitizeLinesForDisplay(String* Lines, uint8_t NumLines)
{
    if (!Config.bPrivateMode)
    {
        return;
    }

    for (uint8_t Index = 0; Index < NumLines; ++Index)
    {
        Lines[Index] = ApplyPrivateMaskToText(Lines[Index]);
    }
}

void FTrackerDevice::ResetAllDataAndApplyDefaults()
{
    Storage.ResetAllUserData();

    Logger.Clear();
    SendLogger.Clear();
    DiagCount = 0;
    DiagScroll = 0;

    memset(&Config, 0, sizeof(Config));
    Config.bPrivateMode = false;
    Config.PrivatePassword[0] = '\0';
    Config.Recipient[0] = '\0';

    PeriodicSettings = FPeriodicSmsSettings{};
    EditPeriodicSettings = PeriodicSettings;
    GpsBoundsSettings = FGpsBoundsSettings{};
    EditGpsBoundsSettings = GpsBoundsSettings;
    LastGsmStatus = FGsmStatus{};
    PrivateModeSelection = 0;
    PasswordPurpose = EPasswordPurpose::None;
    PasswordCursor = 0;
    GpsBoundsSelection = 0;
    GpsBoundsEditPointIndex = 0;
    GpsBoundsEditCursor = 0;
    memset(EditPasswordBuffer, '0', PrivatePasswordLength);
    EditPasswordBuffer[PrivatePasswordLength] = '\0';
    snprintf(EditGpsBoundsBuffer, sizeof(EditGpsBoundsBuffer), "0.0000,0.0000");

    NextPeriodicSmsAtMs = 0;
    bKeepGsmAliveForResend = false;
    bPowerDownWhenIdle = false;
    bShowGsmSendLogs = false;
    bManualSmsInProgress = false;
    bPeriodicSmsInProgress = false;
    Gps.ClearStoredFix();
    Gsm.PowerOff();

    AddDiag("All data reset");
    AddDiag("Recipient: ");
    AddDiag("Private mode: OFF");
    AddDiag("GPS bounds: OFF");
    AddDiag("Periodic SMS: OFF, 30 s");

    BusyTitle = "Data reset";
    BusyLineA = "Phone/GPS erased";
    BusyLineB = "Defaults restored";
    CurrentScreen = EScreen::Message;
}

void FTrackerDevice::BeginPrivateModeMenu()
{
    PrivateModeSelection = 0;
    CurrentScreen = EScreen::PrivateModeMenu;
}

void FTrackerDevice::BeginPasswordEntry(EPasswordPurpose InPurpose)
{
    PasswordPurpose = InPurpose;
    memset(EditPasswordBuffer, '0', PrivatePasswordLength);
    EditPasswordBuffer[PrivatePasswordLength] = '\0';
    PasswordCursor = 0;
    CurrentScreen = EScreen::PasswordEntry;
}

void FTrackerDevice::SubmitPasswordEntry()
{
    if (PasswordPurpose == EPasswordPurpose::EnablePrivateMode)
    {
        strlcpy(Config.PrivatePassword, EditPasswordBuffer, sizeof(Config.PrivatePassword));
        Config.bPrivateMode = true;
        Storage.Save(Config);
        BusyTitle = "Private mode";
        BusyLineA = "Enabled";
        BusyLineB = "Password saved";
        CurrentScreen = EScreen::Message;
    }
    else if (PasswordPurpose == EPasswordPurpose::DisablePrivateMode)
    {
        if (strncmp(Config.PrivatePassword, EditPasswordBuffer, sizeof(Config.PrivatePassword)) == 0)
        {
            Config.bPrivateMode = false;
            Storage.Save(Config);
            BusyTitle = "Private mode";
            BusyLineA = "Disabled";
            BusyLineB = "Password accepted";
            CurrentScreen = EScreen::Message;
        }
        else
        {
            BusyTitle = "Private mode";
            BusyLineA = "Incorrect password";
            BusyLineB = "Please try again";
            CurrentScreen = EScreen::Message;
        }
    }

    PasswordPurpose = EPasswordPurpose::None;
}

void FTrackerDevice::HandlePrivateModeMenuButton(EButtonEvent Button)
{
    const uint8_t MaxSelection = Config.bPrivateMode ? 2 : 1;
    if (Button == EButtonEvent::Up)
    {
        PrivateModeSelection = (PrivateModeSelection == 0) ? MaxSelection : (PrivateModeSelection - 1);
    }
    else if (Button == EButtonEvent::Down)
    {
        PrivateModeSelection = (PrivateModeSelection >= MaxSelection) ? 0 : (PrivateModeSelection + 1);
    }
    else if (Button == EButtonEvent::Back)
    {
        CurrentScreen = EScreen::Menu;
    }
    else if (Button == EButtonEvent::Ok)
    {
        if (!Config.bPrivateMode)
        {
            if (PrivateModeSelection == 0)
            {
                BeginPasswordEntry(EPasswordPurpose::EnablePrivateMode);
            }
            else
            {
                CurrentScreen = EScreen::Menu;
            }
        }
        else
        {
            if (PrivateModeSelection == 0)
            {
                BeginPasswordEntry(EPasswordPurpose::DisablePrivateMode);
            }
            else if (PrivateModeSelection == 1)
            {
                ResetAllDataAndApplyDefaults();
            }
            else
            {
                CurrentScreen = EScreen::Menu;
            }
        }
    }
}

void FTrackerDevice::HandlePasswordEntryButton(EButtonEvent Button)
{
    if (Button == EButtonEvent::Up || Button == EButtonEvent::Down)
    {
        char& CurrentChar = EditPasswordBuffer[PasswordCursor];
        int32_t Digit = (CurrentChar >= '0' && CurrentChar <= '9') ? (CurrentChar - '0') : 0;
        Digit += (Button == EButtonEvent::Up) ? 1 : -1;
        if (Digit < 0)
        {
            Digit = 9;
        }
        if (Digit > 9)
        {
            Digit = 0;
        }
        CurrentChar = static_cast<char>('0' + Digit);
    }
    else if (Button == EButtonEvent::Ok)
    {
        if (PasswordCursor + 1 < PrivatePasswordLength)
        {
            ++PasswordCursor;
        }
        else
        {
            SubmitPasswordEntry();
        }
    }
    else if (Button == EButtonEvent::Back)
    {
        if (PasswordCursor > 0)
        {
            --PasswordCursor;
        }
        else
        {
            PasswordPurpose = EPasswordPurpose::None;
            CurrentScreen = EScreen::PrivateModeMenu;
        }
    }
}

void FTrackerDevice::AddDiag(const String& Line)
{
    if (DiagCount < MaxDiagLines)
    {
        DiagLines[DiagCount++] = Line;
    }
    Logger.Log(ELogLevel::Info, Line);
}

