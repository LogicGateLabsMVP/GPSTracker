#include "GSMModule.h"

#include "Logger.h"

FGSMModule::FGSMModule(FLogger& InLogger)
    : Logger(InLogger)
    , SessionLogger(nullptr)
    , ProgressCallback(nullptr)
    , ProgressContext(nullptr)
    , bPowerControlConfigured(false)
    , bPowerEnabled(false)
    , bBootDrivingGround(false)
    , LastBootPinLevel(LOW)
    , bInitialized(false)
    , CurrentBaud(0)
    , CurrentState(EAsyncState::Idle)
    , PendingAfterInitAction(EAfterInitAction::None)
    , StateStartMs(0)
    , CurrentBaudProbeIndex(0)
    , bRequestActive(false)
    , bRequestCompleted(false)
    , bRequestSuccess(false)
    , RequestStartMs(0)
    , RequestTimeoutMs(0)
    , bStatusRefreshInProgress(false)
    , bCachedStatusDirty(false)
    , CurrentSmsAttempt(1)
    , bSendResultReady(false)
    , bSendResultSuccess(false)
{
}

FGSMModule::~FGSMModule() = default;

void FGSMModule::SetSessionLogger(FLogger* InSessionLogger)
{
    SessionLogger = InSessionLogger;
}

void FGSMModule::SetProgressCallback(void (*InProgressCallback)(void*), void* InProgressContext)
{
    ProgressCallback = InProgressCallback;
    ProgressContext = InProgressContext;
}

bool FGSMModule::Initialize()
{
    StartInitialize();
    return true;
}

void FGSMModule::Update()
{
    UpdateRequest();
    UpdateStateMachine();
}

void FGSMModule::BeginPowerControl()
{
    pinMode(HW::GsmBoot, OUTPUT);
    ApplyBootControl(false, false);

    bPowerControlConfigured = true;
    bPowerEnabled = false;
    bInitialized = false;
    CurrentState = EAsyncState::Idle;
    PendingAfterInitAction = EAfterInitAction::None;
    Log(ELogLevel::Info, String("GSM BOOT control ready on GPIO ") + String(HW::GsmBoot) + ". BC547 mode: GPIO " + (AppConfig::GsmBootEnableLevelLow ? "LOW" : "HIGH") + " grounds BOOT.");
}

bool FGSMModule::PowerOn()
{
    if (!bPowerControlConfigured)
    {
        BeginPowerControl();
    }

    if (bPowerEnabled)
    {
        return true;
    }

    ApplyBootControl(true, true);
    bPowerEnabled = true;
    return true;
}

void FGSMModule::PowerOff()
{
    if (!bPowerControlConfigured)
    {
        return;
    }

    if (IsBusy())
    {
        return;
    }

    if (!bPowerEnabled)
    {
        ResetRuntimeStateToOff(true, false);
        return;
    }

    if (!bInitialized)
    {
        Log(ELogLevel::Warning, "GSM power-off fallback: modem is not initialized, releasing BOOT directly.");
        ResetRuntimeStateToOff(true, true);
        return;
    }

    Log(ELogLevel::Info, "GSM soft power-off requested via AT+CPWROFF.");
    BeginAsyncRequest("AT+CPWROFF", AppConfig::GsmSoftPowerOffTimeoutMs, "POWER DOWN");
    CurrentState = EAsyncState::PowerOffWait;
}

bool FGSMModule::IsPoweredOn()
{
    return bPowerEnabled;
}

bool FGSMModule::IsBootDrivingGround()
{
    return bBootDrivingGround;
}

uint8_t FGSMModule::GetBootPinLevel()
{
    return LastBootPinLevel;
}

bool FGSMModule::IsInitialized()
{
    return bInitialized;
}

bool FGSMModule::IsBusy()
{
    return CurrentState != EAsyncState::Idle || bRequestActive || bRequestCompleted;
}

bool FGSMModule::IsStatusRefreshInProgress()
{
    return bStatusRefreshInProgress;
}

bool FGSMModule::StartInitialize()
{
    if (IsBusy())
    {
        return false;
    }

    PowerOn();
    CurrentBaudProbeIndex = 0;
    CurrentState = EAsyncState::InitWaitPower;
    StateStartMs = millis();
    Log(ELogLevel::Info, "GSM init started.");
    return true;
}

bool FGSMModule::EnsureInitializedAsync(EAfterInitAction InAfterInitAction)
{
    if (bInitialized)
    {
        if (InAfterInitAction == EAfterInitAction::StatusRefresh)
        {
            StartStatusRefreshInternal();
        }
        else if (InAfterInitAction == EAfterInitAction::SendSms)
        {
            StartSendSmsInternal();
        }
        return true;
    }

    if (IsBusy())
    {
        return false;
    }

    PendingAfterInitAction = InAfterInitAction;
    return StartInitialize();
}

bool FGSMModule::RequestStatusRefresh()
{
    if (bStatusRefreshInProgress || IsBusy())
    {
        return false;
    }

    if (!bInitialized)
    {
        return EnsureInitializedAsync(EAfterInitAction::StatusRefresh);
    }

    StartStatusRefreshInternal();
    return true;
}

bool FGSMModule::ConsumeStatusUpdate(FGsmStatus& OutStatus)
{
    if (!bCachedStatusDirty)
    {
        return false;
    }

    OutStatus = CachedStatus;
    bCachedStatusDirty = false;
    return true;
}

FGsmStatus FGSMModule::GetUiStatusSnapshot()
{
    FGsmStatus Snapshot = CachedStatus;

    if (!bPowerEnabled)
    {
        Snapshot.bResponsive = false;
        Snapshot.SimState = "GSM disabled";
        Snapshot.NetworkState = "GSM disabled";
        Snapshot.OperatorName = "GSM disabled";
        Snapshot.Iccid = "N/A";
        Snapshot.Csq = -1;
        return Snapshot;
    }

    if (CurrentState == EAsyncState::PowerOffWait || CurrentState == EAsyncState::PowerOffGrace)
    {
        Snapshot.bResponsive = false;
        Snapshot.SimState = "Powering off";
        Snapshot.NetworkState = "Powering off";
        Snapshot.OperatorName = "Powering off";
        Snapshot.Csq = -1;
        return Snapshot;
    }

    const bool bInitInProgress =
        CurrentState == EAsyncState::InitWaitPower ||
        CurrentState == EAsyncState::InitBaudStart ||
        CurrentState == EAsyncState::InitBaudSettle ||
        CurrentState == EAsyncState::InitProbeAT ||
        CurrentState == EAsyncState::InitATE0 ||
        CurrentState == EAsyncState::InitCMEE;

    if (bInitInProgress)
    {
        Snapshot.bResponsive = false;
        Snapshot.SimState = "Initializing";
        Snapshot.NetworkState = "Starting";
        Snapshot.OperatorName = "Unknown";
        Snapshot.Csq = -1;
    }

    if (bStatusRefreshInProgress)
    {
        if (PendingStatus.bResponsive)
        {
            Snapshot.bResponsive = PendingStatus.bResponsive;
        }
        if (PendingStatus.SimState.length() > 0 && PendingStatus.SimState != "Unknown")
        {
            Snapshot.SimState = PendingStatus.SimState;
        }
        if (PendingStatus.NetworkState.length() > 0 && PendingStatus.NetworkState != "Unknown")
        {
            Snapshot.NetworkState = PendingStatus.NetworkState;
        }
        if (PendingStatus.OperatorName.length() > 0 && PendingStatus.OperatorName != "Unknown")
        {
            Snapshot.OperatorName = PendingStatus.OperatorName;
        }
        if (PendingStatus.Iccid.length() > 0 && PendingStatus.Iccid != "N/A")
        {
            Snapshot.Iccid = PendingStatus.Iccid;
        }
        if (PendingStatus.Csq >= 0)
        {
            Snapshot.Csq = PendingStatus.Csq;
        }
    }

    const String LiveSimState = ParseSimState(RequestResponse);
    if (LiveSimState != "Unknown")
    {
        Snapshot.SimState = LiveSimState;
    }

    const String LiveNetworkState = ParseNetworkState(RequestResponse);
    if (LiveNetworkState != "Unknown")
    {
        Snapshot.NetworkState = LiveNetworkState;
    }

    const String ParsedOperator = ParseQuotedString(RequestResponse);
    if (ParsedOperator.length() > 0)
    {
        Snapshot.OperatorName = ParsedOperator;
    }

    const int32_t LiveCsq = ParseCsq(RequestResponse);
    if (LiveCsq >= 0)
    {
        Snapshot.Csq = LiveCsq;
    }

    if (IsSimBusyResponse(RequestResponse))
    {
        Snapshot.SimState = "SIM busy";
    }

    if (!bInitialized && Snapshot.SimState == "Unknown")
    {
        Snapshot.SimState = "Initializing";
    }
    if (!bInitialized && Snapshot.NetworkState == "Unknown")
    {
        Snapshot.NetworkState = "Starting";
    }
    if (Snapshot.OperatorName.length() == 0)
    {
        Snapshot.OperatorName = "Unknown";
    }

    return Snapshot;
}

bool FGSMModule::StartSendSms(const char* Recipient, const String& Message)
{
    if (Recipient == nullptr || strlen(Recipient) == 0)
    {
        Log(ELogLevel::Error, "Recipient number is empty.");
        bSendResultReady = true;
        bSendResultSuccess = false;
        return false;
    }

    if (IsBusy())
    {
        Log(ELogLevel::Warning, "GSM is busy. Cannot start SMS send right now.");
        return false;
    }

    PendingRecipient = Recipient;
    PendingSmsMessage = Message;
    CurrentSmsAttempt = 1;
    bSendResultReady = false;
    bSendResultSuccess = false;

    if (!bInitialized)
    {
        return EnsureInitializedAsync(EAfterInitAction::SendSms);
    }

    StartSendSmsInternal();
    return true;
}

bool FGSMModule::ConsumeSendSmsResult(bool& OutSuccess)
{
    if (!bSendResultReady)
    {
        return false;
    }

    OutSuccess = bSendResultSuccess;
    bSendResultReady = false;
    return true;
}

void FGSMModule::Log(ELogLevel Level, const String& Message)
{
    Logger.Log(Level, Message);
    if (SessionLogger != nullptr)
    {
        SessionLogger->Log(Level, Message);
    }
    PumpProgress();
}

void FGSMModule::PumpProgress()
{
    if (ProgressCallback != nullptr)
    {
        ProgressCallback(ProgressContext);
    }
}

uint8_t FGSMModule::GetBootGpioLevelForEnable()
{
    return AppConfig::GsmBootEnableLevelLow ? LOW : HIGH;
}

uint8_t FGSMModule::GetBootGpioLevelForDisable()
{
    return AppConfig::GsmBootEnableLevelLow ? HIGH : LOW;
}

void FGSMModule::ApplyBootControl(bool bEnableGsm, bool bLogChange)
{
    const uint8_t NewLevel = bEnableGsm ? GetBootGpioLevelForEnable() : GetBootGpioLevelForDisable();
    digitalWrite(HW::GsmBoot, NewLevel);
    LastBootPinLevel = NewLevel;
    bBootDrivingGround = bEnableGsm;

    if (bLogChange)
    {
        Log(ELogLevel::Info, String("GSM BOOT GPIO") + String(HW::GsmBoot) + " -> " + (NewLevel == HIGH ? "HIGH" : "LOW") + (bEnableGsm ? " (BC547 ON, BOOT->GND)" : " (BC547 OFF, BOOT released)"));
    }
}

void FGSMModule::ResetRuntimeStateToOff(bool bCloseSerial, bool bReleaseBoot)
{
    if (bReleaseBoot)
    {
        ApplyBootControl(false, true);
    }

    if (bCloseSerial)
    {
        Serial1.end();
    }

    bPowerEnabled = false;
    bInitialized = false;
    PendingAfterInitAction = EAfterInitAction::None;
    CurrentState = EAsyncState::Idle;
    ClearRequest();
    bStatusRefreshInProgress = false;
    CurrentBaud = 0;
}

void FGSMModule::ClearRequest()
{
    bRequestActive = false;
    bRequestCompleted = false;
    bRequestSuccess = false;
    RequestResponse = "";
    RequestSuccessToken = "";
    RequestStartMs = 0;
    RequestTimeoutMs = 0;
}

void FGSMModule::FlushInput()
{
    while (Serial1.available() > 0)
    {
        Serial1.read();
    }
}

void FGSMModule::BeginAsyncRequest(const String& Command, uint32_t TimeoutMs, const String& SuccessToken)
{
    FlushInput();
    Serial1.print(Command);
    Serial1.print("\r");

    bRequestActive = true;
    bRequestCompleted = false;
    bRequestSuccess = false;
    RequestResponse = "";
    RequestSuccessToken = SuccessToken;
    RequestStartMs = millis();
    RequestTimeoutMs = TimeoutMs;
    PumpProgress();
}

void FGSMModule::BeginAsyncWait(uint32_t TimeoutMs, const String& SuccessToken)
{
    FlushInput();
    bRequestActive = true;
    bRequestCompleted = false;
    bRequestSuccess = false;
    RequestResponse = "";
    RequestSuccessToken = SuccessToken;
    RequestStartMs = millis();
    RequestTimeoutMs = TimeoutMs;
    PumpProgress();
}

void FGSMModule::UpdateRequest()
{
    if (!bRequestActive)
    {
        return;
    }

    while (Serial1.available() > 0)
    {
        RequestResponse += static_cast<char>(Serial1.read());
    }

    if (RequestSuccessToken.length() > 0 && RequestResponse.indexOf(RequestSuccessToken) >= 0)
    {
        bRequestActive = false;
        bRequestCompleted = true;
        bRequestSuccess = true;
        return;
    }

    if (RequestResponse.indexOf("ERROR") >= 0 || RequestResponse.indexOf("+CME ERROR") >= 0 || RequestResponse.indexOf("+CMS ERROR") >= 0)
    {
        bRequestActive = false;
        bRequestCompleted = true;
        bRequestSuccess = false;
        return;
    }

    if ((millis() - RequestStartMs) >= RequestTimeoutMs)
    {
        bRequestActive = false;
        bRequestCompleted = true;
        bRequestSuccess = false;
    }
}

bool FGSMModule::ConsumeRequestResult(bool& OutSuccess, String& OutResponse)
{
    if (!bRequestCompleted)
    {
        return false;
    }

    OutSuccess = bRequestSuccess;
    OutResponse = RequestResponse;
    bRequestCompleted = false;
    return true;
}

void FGSMModule::CompleteIdle()
{
    CurrentState = EAsyncState::Idle;
    StateStartMs = 0;
    PumpProgress();
}

void FGSMModule::CompleteInit(bool bSuccess)
{
    if (bSuccess)
    {
        bInitialized = true;
        Log(ELogLevel::Info, "GSM init finished successfully.");
    }
    else
    {
        bInitialized = false;
        bPowerEnabled = false;
        Log(ELogLevel::Error, "GSM init failed.");
    }

    const EAfterInitAction AfterInitAction = PendingAfterInitAction;
    PendingAfterInitAction = EAfterInitAction::None;

    if (bSuccess)
    {
        if (AfterInitAction == EAfterInitAction::StatusRefresh)
        {
            StartStatusRefreshInternal();
            return;
        }
        if (AfterInitAction == EAfterInitAction::SendSms)
        {
            StartSendSmsInternal();
            return;
        }
    }
    else
    {
        if (AfterInitAction == EAfterInitAction::StatusRefresh)
        {
            CachedStatus = FGsmStatus{};
            bCachedStatusDirty = true;
        }
        else if (AfterInitAction == EAfterInitAction::SendSms)
        {
            bSendResultReady = true;
            bSendResultSuccess = false;
        }
    }

    CompleteIdle();
}

void FGSMModule::StartStatusRefreshInternal()
{
    PendingStatus = FGsmStatus{};
    bStatusRefreshInProgress = true;
    CurrentState = EAsyncState::StatusAT;
    BeginAsyncRequest("AT", 1000, "OK");
    Log(ELogLevel::Info, "GSM status refresh started.");
}

void FGSMModule::FinishStatusRefresh()
{
    CachedStatus = PendingStatus;
    bCachedStatusDirty = true;
    bStatusRefreshInProgress = false;
    CompleteIdle();
}

void FGSMModule::StartSendSmsInternal()
{
    CurrentState = EAsyncState::SendCMGF;
    Log(ELogLevel::Info, "GSM SMS send started.");
    Log(ELogLevel::Info, "GSM attempt " + String(CurrentSmsAttempt) + "/" + String(MaxSmsAttempts) + ".");
    BeginAsyncRequest("AT+CMGF=1", 1500, "OK");
}

bool FGSMModule::IsSimBusyResponse(const String& Text)
{
    String LowerText = Text;
    LowerText.toLowerCase();
    return LowerText.indexOf("sim busy") >= 0;
}

void FGSMModule::ScheduleRetry(const String& Stage, const String& Response)
{
    if (IsSimBusyResponse(Response) && CurrentSmsAttempt < MaxSmsAttempts)
    {
        ++CurrentSmsAttempt;
        Log(ELogLevel::Warning, Stage + ": SIM busy.");
        Log(ELogLevel::Info, "Retrying in " + String(AppConfig::GsmPeriodicRetryMs / 1000) + " s.");
        CurrentState = EAsyncState::RetryDelay;
        StateStartMs = millis();
        return;
    }

    Log(ELogLevel::Error, Stage + " failed: " + Response);
    bSendResultSuccess = false;
    bSendResultReady = true;
    CompleteIdle();
}

void FGSMModule::FinishSendSms(bool bSuccess)
{
    bSendResultSuccess = bSuccess;
    bSendResultReady = true;
    if (bSuccess)
    {
        Log(ELogLevel::Info, "SMS sent successfully.");
    }
    CompleteIdle();
}

void FGSMModule::UpdateStateMachine()
{
    bool bRequestOk = false;
    String Response;

    switch (CurrentState)
    {
        case EAsyncState::Idle:
            return;

        case EAsyncState::InitWaitPower:
            if ((millis() - StateStartMs) >= AppConfig::GsmPowerSettleMs)
            {
                CurrentState = EAsyncState::InitBaudStart;
            }
            return;

        case EAsyncState::InitBaudStart:
            if (CurrentBaudProbeIndex >= ProbeBaudCount)
            {
                Log(ELogLevel::Error, "GSM modem did not answer on any common baud rate.");
                CompleteInit(false);
                return;
            }

            Serial1.end();
            Serial1.begin(ProbeBauds[CurrentBaudProbeIndex], SERIAL_8N1, HW::GsmRx, HW::GsmTx);
            StateStartMs = millis();
            CurrentState = EAsyncState::InitBaudSettle;
            return;

        case EAsyncState::InitBaudSettle:
            if ((millis() - StateStartMs) >= 80)
            {
                BeginAsyncRequest("AT", 1200, "OK");
                CurrentState = EAsyncState::InitProbeAT;
            }
            return;

        case EAsyncState::InitProbeAT:
            if (!ConsumeRequestResult(bRequestOk, Response))
            {
                return;
            }

            if (bRequestOk)
            {
                CurrentBaud = ProbeBauds[CurrentBaudProbeIndex];
                Log(ELogLevel::Info, "GSM modem responded at " + String(CurrentBaud) + " baud.");
                BeginAsyncRequest("ATE0", 1000, "OK");
                CurrentState = EAsyncState::InitATE0;
                return;
            }

            ++CurrentBaudProbeIndex;
            CurrentState = EAsyncState::InitBaudStart;
            return;

        case EAsyncState::InitATE0:
            if (!ConsumeRequestResult(bRequestOk, Response))
            {
                return;
            }
            BeginAsyncRequest("AT+CMEE=2", 1000, "OK");
            CurrentState = EAsyncState::InitCMEE;
            return;

        case EAsyncState::InitCMEE:
            if (!ConsumeRequestResult(bRequestOk, Response))
            {
                return;
            }
            CompleteInit(true);
            return;

        case EAsyncState::StatusAT:
            if (!ConsumeRequestResult(bRequestOk, Response))
            {
                return;
            }
            if (!bRequestOk)
            {
                PendingStatus = FGsmStatus{};
                bInitialized = false;
                FinishStatusRefresh();
                return;
            }
            PendingStatus.bResponsive = true;
            BeginAsyncRequest("AT+CPIN?", 1200, "OK");
            CurrentState = EAsyncState::StatusCPIN;
            return;

        case EAsyncState::StatusCPIN:
            if (!ConsumeRequestResult(bRequestOk, Response))
            {
                return;
            }
            PendingStatus.SimState = ParseSimState(Response);
            BeginAsyncRequest("AT+CCID", 1200, "OK");
            CurrentState = EAsyncState::StatusCCID;
            return;

        case EAsyncState::StatusCCID:
            if (!ConsumeRequestResult(bRequestOk, Response))
            {
                return;
            }
            PendingStatus.Iccid = ParseAfterToken(Response, "+CCID:");
            BeginAsyncRequest("AT+CREG?", 1200, "OK");
            CurrentState = EAsyncState::StatusCREG;
            return;

        case EAsyncState::StatusCREG:
            if (!ConsumeRequestResult(bRequestOk, Response))
            {
                return;
            }
            PendingStatus.NetworkState = ParseNetworkState(Response);
            BeginAsyncRequest("AT+CSQ", 1200, "OK");
            CurrentState = EAsyncState::StatusCSQ;
            return;

        case EAsyncState::StatusCSQ:
            if (!ConsumeRequestResult(bRequestOk, Response))
            {
                return;
            }
            PendingStatus.Csq = ParseCsq(Response);
            BeginAsyncRequest("AT+COPS?", 2000, "OK");
            CurrentState = EAsyncState::StatusCOPS;
            return;

        case EAsyncState::StatusCOPS:
            if (!ConsumeRequestResult(bRequestOk, Response))
            {
                return;
            }
            {
                const String ParsedOperator = ParseQuotedString(Response);
                if (ParsedOperator.length() > 0)
                {
                    PendingStatus.OperatorName = ParsedOperator;
                }
            }
            FinishStatusRefresh();
            return;

        case EAsyncState::SendCMGF:
            if (!ConsumeRequestResult(bRequestOk, Response))
            {
                return;
            }
            if (!bRequestOk)
            {
                ScheduleRetry("AT+CMGF", Response);
                return;
            }
            BeginAsyncRequest("AT+CSCS=\"GSM\"", 1500, "OK");
            CurrentState = EAsyncState::SendCSCS;
            return;

        case EAsyncState::SendCSCS:
            if (!ConsumeRequestResult(bRequestOk, Response))
            {
                return;
            }
            if (!bRequestOk)
            {
                ScheduleRetry("AT+CSCS", Response);
                return;
            }
            BeginAsyncRequest(String("AT+CMGS=\"") + PendingRecipient + "\"", 4000, ">");
            CurrentState = EAsyncState::SendCMGS;
            return;

        case EAsyncState::SendCMGS:
            if (!ConsumeRequestResult(bRequestOk, Response))
            {
                return;
            }
            if (!bRequestOk)
            {
                ScheduleRetry("AT+CMGS", Response);
                return;
            }
            Log(ELogLevel::Info, "Prompt received. Writing SMS body.");
            Serial1.print(PendingSmsMessage);
            Serial1.write(0x1A);
            BeginAsyncWait(15000, "+CMGS:");
            CurrentState = EAsyncState::SendSubmit;
            return;

        case EAsyncState::SendSubmit:
            if (!ConsumeRequestResult(bRequestOk, Response))
            {
                return;
            }
            if (!bRequestOk)
            {
                ScheduleRetry("SMS submit", Response);
                return;
            }
            FinishSendSms(true);
            return;

        case EAsyncState::RetryDelay:
            if ((millis() - StateStartMs) >= AppConfig::GsmPeriodicRetryMs)
            {
                CurrentState = EAsyncState::SendCMGF;
                Log(ELogLevel::Info, "GSM attempt " + String(CurrentSmsAttempt) + "/" + String(MaxSmsAttempts) + ".");
                BeginAsyncRequest("AT+CMGF=1", 1500, "OK");
            }
            return;

        case EAsyncState::PowerOffWait:
            if (!ConsumeRequestResult(bRequestOk, Response))
            {
                return;
            }

            if (!bRequestOk && Response.length() > 0)
            {
                Log(ELogLevel::Warning, "AT+CPWROFF response: " + Response);
            }

            StateStartMs = millis();
            CurrentState = EAsyncState::PowerOffGrace;
            return;

        case EAsyncState::PowerOffGrace:
            if ((millis() - StateStartMs) >= AppConfig::GsmSoftPowerOffGraceMs)
            {
                Log(ELogLevel::Info, "GSM soft power-off finished.");
                ResetRuntimeStateToOff(true, true);
            }
            return;
    }
}

String FGSMModule::ParseSimState(const String& Text)
{
    String UpperText = Text;
    UpperText.toUpperCase();

    if (UpperText.indexOf("SIM BUSY") >= 0)      return "SIM busy";
    if (UpperText.indexOf("READY") >= 0)         return "READY";
    if (UpperText.indexOf("SIM PIN") >= 0)       return "PIN required";
    if (UpperText.indexOf("SIM PUK") >= 0)       return "PUK required";
    if (UpperText.indexOf("NOT READY") >= 0)     return "SIM not ready";
    if (UpperText.indexOf("NOT INSERTED") >= 0)  return "SIM missing";
    if (UpperText.indexOf("ERROR") >= 0)         return "SIM error";
    if (UpperText.indexOf("POWER DOWN") >= 0)    return "GSM disabled";
    return "Unknown";
}

String FGSMModule::ParseNetworkState(const String& Text)
{
    String UpperText = Text;
    UpperText.toUpperCase();

    if (UpperText.indexOf("SIM BUSY") >= 0)
    {
        return "Busy";
    }
    if (UpperText.indexOf("POWER DOWN") >= 0)
    {
        return "GSM disabled";
    }

    const int Start = UpperText.indexOf("+CREG:");
    if (Start >= 0)
    {
        int End = UpperText.indexOf('\n', Start);
        if (End < 0)
        {
            End = UpperText.length();
        }

        String Value = UpperText.substring(Start + 6, End);
        Value.trim();
        const int Comma = Value.lastIndexOf(',');
        String CodeString = (Comma >= 0) ? Value.substring(Comma + 1) : Value;
        CodeString.trim();
        const char Code = CodeString.length() > 0 ? CodeString[0] : '\0';
        switch (Code)
        {
            case '1': return "Registered";
            case '5': return "Roaming";
            case '2': return "Searching";
            case '3': return "Denied";
            case '0': return "Not registered";
            default:  break;
        }
    }

    if (UpperText.indexOf("ROAM") >= 0)            return "Roaming";
    if (UpperText.indexOf("SEARCH") >= 0)          return "Searching";
    if (UpperText.indexOf("DENIED") >= 0)          return "Denied";
    if (UpperText.indexOf("REGISTERED") >= 0)      return "Registered";
    if (UpperText.indexOf("NOT REGISTERED") >= 0)  return "Not registered";
    return "Unknown";
}

int32_t FGSMModule::ParseCsq(const String& Text)
{
    const int Start = Text.indexOf("+CSQ:");
    if (Start < 0)
    {
        return -1;
    }

    const int Comma = Text.indexOf(',', Start);
    if (Comma < 0)
    {
        return -1;
    }

    String Number = Text.substring(Start + 5, Comma);
    Number.trim();
    return Number.toInt();
}

String FGSMModule::ParseAfterToken(const String& Text, const String& Token)
{
    const int Start = Text.indexOf(Token);
    if (Start < 0)
    {
        return "N/A";
    }

    int End = Text.indexOf('\n', Start);
    if (End < 0)
    {
        End = Text.length();
    }

    String Value = Text.substring(Start + Token.length(), End);
    Value.trim();
    return Value.length() > 0 ? Value : "N/A";
}

String FGSMModule::ParseQuotedString(const String& Text)
{
    const int FirstQuote = Text.indexOf('"');
    if (FirstQuote < 0)
    {
        return "";
    }

    const int SecondQuote = Text.indexOf('"', FirstQuote + 1);
    if (SecondQuote < 0)
    {
        return "";
    }

    return Text.substring(FirstQuote + 1, SecondQuote);
}

