#pragma once

#include "Config.h"
#include "DeviceModule.h"

#include <U8g2lib.h>

class FDisplayModule : public IDeviceModule
{
public:
    FDisplayModule();
    ~FDisplayModule() override;

    bool Initialize() override;
    void Update() override;

    EButtonEvent ConsumeButtonEvent();
    void DrawLines(const String& Title, const String* Lines, uint8_t NumLines, int16_t ScrollOffset = 0, int16_t HighlightLine = -1);
    void DrawMessage(const String& Title, const String& LineA, const String& LineB = "", const String& LineC = "");
    void DrawRecipientEditor(const char* Buffer, uint8_t Cursor);
    void DrawPasswordEditor(const String& Title, const char* Buffer, uint8_t Cursor);
    void DrawTextEditor(const String& Title, const char* Buffer, uint8_t Cursor, const String& FooterA, const String& FooterB, const String& FooterC = "");

    bool BeginCustomFrame();
    void EndCustomFrame();
    U8G2_SSD1306_128X64_NONAME_F_HW_I2C& GetNativeDisplay();

private:
    struct FButtonState
    {
        uint8_t Pin = 255;
        bool bStablePressed = false;
        bool bLastRawPressed = false;
        bool bPressedEvent = false;
        uint32_t LastChangeTimeMs = 0;
    };

    bool ProbeDisplay();
    bool TryConnectDisplay(bool bForce);
    bool EnsureDisplayReady();
    void ConfigureButton(uint8_t Index, uint8_t Pin);
    void UpdateButton(uint8_t Index);
    static bool ReadPressed(uint8_t Pin);

private:
    U8G2_SSD1306_128X64_NONAME_F_HW_I2C Display;
    FButtonState Buttons[4];
    bool bDisplayInitialized;
    uint32_t LastProbeMs;
};
