#pragma once

#include "Config.h"
#include "DeviceModule.h"

class FLogger;

class FGSMModule : public IDeviceModule
{
public:
    explicit FGSMModule(FLogger& InLogger);
    ~FGSMModule() override;

    void SetSessionLogger(FLogger* InSessionLogger);
    void SetProgressCallback(void (*InProgressCallback)(void*), void* InProgressContext);

    bool Initialize() override;
    void Update() override;

    void BeginPowerControl();
    bool PowerOn();
    void PowerOff();
    bool IsPoweredOn();
    bool IsBootDrivingGround();
    uint8_t GetBootPinLevel();
    bool IsInitialized();
    bool IsBusy();
    bool IsStatusRefreshInProgress();
    bool StartInitialize();
    bool EnsureInitializedAsync(EAfterInitAction InAfterInitAction);
    bool RequestStatusRefresh();
    bool ConsumeStatusUpdate(FGsmStatus& OutStatus);
    FGsmStatus GetUiStatusSnapshot();
    bool StartSendSms(const char* Recipient, const String& Message);
    bool ConsumeSendSmsResult(bool& OutSuccess);

private:
    enum class EAsyncState : uint8_t
    {
        Idle,
        InitWaitPower,
        InitBaudStart,
        InitBaudSettle,
        InitProbeAT,
        InitATE0,
        InitCMEE,
        StatusAT,
        StatusCPIN,
        StatusCCID,
        StatusCREG,
        StatusCSQ,
        StatusCOPS,
        SendCMGF,
        SendCSCS,
        SendCMGS,
        SendSubmit,
        RetryDelay,
        PowerOffWait,
        PowerOffGrace
    };

    void Log(ELogLevel Level, const String& Message);
    void PumpProgress();
    uint8_t GetBootGpioLevelForEnable();
    uint8_t GetBootGpioLevelForDisable();
    void ApplyBootControl(bool bEnableGsm, bool bLogChange);
    void ResetRuntimeStateToOff(bool bCloseSerial, bool bReleaseBoot);
    void ClearRequest();
    void FlushInput();
    void BeginAsyncRequest(const String& Command, uint32_t TimeoutMs, const String& SuccessToken);
    void BeginAsyncWait(uint32_t TimeoutMs, const String& SuccessToken);
    void UpdateRequest();
    bool ConsumeRequestResult(bool& OutSuccess, String& OutResponse);
    void CompleteIdle();
    void CompleteInit(bool bSuccess);
    void StartStatusRefreshInternal();
    void FinishStatusRefresh();
    void StartSendSmsInternal();
    bool IsSimBusyResponse(const String& Text);
    void ScheduleRetry(const String& Stage, const String& Response);
    void FinishSendSms(bool bSuccess);
    void UpdateStateMachine();

    static String ParseSimState(const String& Text);
    static String ParseNetworkState(const String& Text);
    static int32_t ParseCsq(const String& Text);
    static String ParseAfterToken(const String& Text, const String& Token);
    static String ParseQuotedString(const String& Text);

private:
    static constexpr uint32_t ProbeBauds[] = {115200, 9600, 57600, 38400, 19200};
    static constexpr uint8_t ProbeBaudCount = sizeof(ProbeBauds) / sizeof(ProbeBauds[0]);
    static constexpr uint8_t MaxSmsAttempts = 10;

    FLogger& Logger;
    FLogger* SessionLogger;
    void (*ProgressCallback)(void*);
    void* ProgressContext;

    bool bPowerControlConfigured;
    bool bPowerEnabled;
    bool bBootDrivingGround;
    uint8_t LastBootPinLevel;
    bool bInitialized;
    uint32_t CurrentBaud;

    EAsyncState CurrentState;
    EAfterInitAction PendingAfterInitAction;
    uint32_t StateStartMs;
    uint8_t CurrentBaudProbeIndex;

    bool bRequestActive;
    bool bRequestCompleted;
    bool bRequestSuccess;
    String RequestResponse;
    String RequestSuccessToken;
    uint32_t RequestStartMs;
    uint32_t RequestTimeoutMs;

    bool bStatusRefreshInProgress;
    bool bCachedStatusDirty;
    FGsmStatus PendingStatus;
    FGsmStatus CachedStatus;

    String PendingRecipient;
    String PendingSmsMessage;
    uint8_t CurrentSmsAttempt;
    bool bSendResultReady;
    bool bSendResultSuccess;
};
