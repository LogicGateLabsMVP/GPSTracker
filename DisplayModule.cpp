#include "DisplayModule.h"

#include <Wire.h>
#include <U8g2lib.h>

FDisplayModule::FDisplayModule()
    : Display(U8G2_R0, U8X8_PIN_NONE)
    , bDisplayInitialized(false)
    , LastProbeMs(0)
{
}

FDisplayModule::~FDisplayModule() = default;

bool FDisplayModule::Initialize()
{
    Wire.begin(HW::I2cSda, HW::I2cScl, AppConfig::DisplayI2cHz);

    ConfigureButton(0, HW::KeyUp);
    ConfigureButton(1, HW::KeyDown);
    ConfigureButton(2, HW::KeyOk);
    ConfigureButton(3, HW::KeyBack);

    TryConnectDisplay(true);
    return true;
}

void FDisplayModule::Update()
{
    for (uint8_t ButtonIndex = 0; ButtonIndex < 4; ++ButtonIndex)
    {
        UpdateButton(ButtonIndex);
    }
}

EButtonEvent FDisplayModule::ConsumeButtonEvent()
{
    if (Buttons[0].bPressedEvent) { Buttons[0].bPressedEvent = false; return EButtonEvent::Up;   }
    if (Buttons[1].bPressedEvent) { Buttons[1].bPressedEvent = false; return EButtonEvent::Down; }
    if (Buttons[2].bPressedEvent) { Buttons[2].bPressedEvent = false; return EButtonEvent::Ok;   }
    if (Buttons[3].bPressedEvent) { Buttons[3].bPressedEvent = false; return EButtonEvent::Back; }
    return EButtonEvent::None;
}

void FDisplayModule::DrawLines(const String& Title, const String* Lines, uint8_t NumLines, int16_t ScrollOffset, int16_t HighlightLine)
{
    if (!EnsureDisplayReady())
    {
        return;
    }

    Display.clearBuffer();
    Display.setFont(u8g2_font_6x12_tf);
    Display.drawStr(0, 10, Title.c_str());
    Display.drawHLine(0, 12, 128);
    Display.setFont(u8g2_font_5x7_tf);

    const int16_t VisibleRows = 6;
    for (int16_t Row = 0; Row < VisibleRows; ++Row)
    {
        const int16_t Index = ScrollOffset + Row;
        if (Index < 0 || Index >= NumLines)
        {
            continue;
        }

        const int16_t Y = 22 + Row * 7;
        if (Index == HighlightLine)
        {
            Display.drawBox(0, Y - 6, 128, 8);
            Display.setDrawColor(0);
            Display.drawStr(2, Y, Lines[Index].c_str());
            Display.setDrawColor(1);
        }
        else
        {
            Display.drawStr(2, Y, Lines[Index].c_str());
        }
    }

    Display.sendBuffer();
}

void FDisplayModule::DrawMessage(const String& Title, const String& LineA, const String& LineB, const String& LineC)
{
    String Lines[3] = { LineA, LineB, LineC };
    DrawLines(Title, Lines, 3, 0, -1);
}

void FDisplayModule::DrawRecipientEditor(const char* Buffer, uint8_t Cursor)
{
    if (!EnsureDisplayReady())
    {
        return;
    }

    Display.clearBuffer();
    Display.setFont(u8g2_font_6x12_tf);
    Display.drawStr(0, 10, "Set recipient");
    Display.drawHLine(0, 12, 128);
    Display.drawStr(0, 28, Buffer);
    Display.drawHLine(Cursor * 6, 30, 6);
    Display.setFont(u8g2_font_5x7_tf);
    Display.drawStr(0, 42, "Up/Down = char");
    Display.drawStr(0, 49, "OK = next/save");
    Display.drawStr(0, 56, "Back = prev/cancel");
    Display.sendBuffer();
}

void FDisplayModule::DrawPasswordEditor(const String& Title, const char* Buffer, uint8_t Cursor)
{
    if (!EnsureDisplayReady())
    {
        return;
    }

    Display.clearBuffer();
    Display.setFont(u8g2_font_6x12_tf);
    Display.drawStr(0, 10, Title.c_str());
    Display.drawHLine(0, 12, 128);
    Display.drawStr(0, 28, Buffer);
    Display.drawHLine(Cursor * 6, 30, 6);
    Display.setFont(u8g2_font_5x7_tf);
    Display.drawStr(0, 42, "Up/Down = digit");
    Display.drawStr(0, 49, "OK = next/enter");
    Display.drawStr(0, 56, "Back = prev/cancel");
    Display.sendBuffer();
}

void FDisplayModule::DrawTextEditor(const String& Title, const char* Buffer, uint8_t Cursor, const String& FooterA, const String& FooterB, const String& FooterC)
{
    if (!EnsureDisplayReady())
    {
        return;
    }

    Display.clearBuffer();
    Display.setFont(u8g2_font_6x12_tf);
    Display.drawStr(0, 10, Title.c_str());
    Display.drawHLine(0, 12, 128);
    Display.drawStr(0, 28, Buffer);
    Display.drawHLine(Cursor * 6, 30, 6);
    Display.setFont(u8g2_font_5x7_tf);
    Display.drawStr(0, 42, FooterA.c_str());
    Display.drawStr(0, 49, FooterB.c_str());
    Display.drawStr(0, 56, FooterC.c_str());
    Display.sendBuffer();
}

bool FDisplayModule::BeginCustomFrame()
{
    if (!EnsureDisplayReady())
    {
        return false;
    }

    Display.clearBuffer();
    return true;
}

void FDisplayModule::EndCustomFrame()
{
    if (!bDisplayInitialized)
    {
        return;
    }

    Display.sendBuffer();
}

U8G2_SSD1306_128X64_NONAME_F_HW_I2C& FDisplayModule::GetNativeDisplay()
{
    return Display;
}


bool FDisplayModule::ProbeDisplay()
{
    Wire.beginTransmission(0x3C);
    return Wire.endTransmission() == 0;
}

bool FDisplayModule::TryConnectDisplay(bool bForce)
{
    const uint32_t NowMs = millis();
    if (!bForce && (NowMs - LastProbeMs) < AppConfig::DisplayProbeMs)
    {
        return bDisplayInitialized;
    }

    LastProbeMs = NowMs;

    const bool bDetected = ProbeDisplay();
    if (!bDetected)
    {
        bDisplayInitialized = false;
        return false;
    }

    if (!bDisplayInitialized)
    {
        Display.setI2CAddress(0x3C * 2);
        Display.begin();
        Display.setPowerSave(0);
        Display.clearBuffer();
        Display.setFont(u8g2_font_6x12_tf);
        Display.drawStr(0, 12, "ESP32-H2 booting...");
        Display.sendBuffer();
        bDisplayInitialized = true;
    }

    return true;
}

bool FDisplayModule::EnsureDisplayReady()
{
    return TryConnectDisplay(false);
}

void FDisplayModule::ConfigureButton(uint8_t Index, uint8_t Pin)
{
    Buttons[Index].Pin = Pin;
    pinMode(Pin, AppConfig::ButtonsActiveLow ? INPUT_PULLUP : INPUT);
    const bool bInitialPressed = ReadPressed(Pin);
    Buttons[Index].bStablePressed = bInitialPressed;
    Buttons[Index].bLastRawPressed = bInitialPressed;
    Buttons[Index].LastChangeTimeMs = millis();
}

void FDisplayModule::UpdateButton(uint8_t Index)
{
    FButtonState& Button = Buttons[Index];
    const bool bRawPressed = ReadPressed(Button.Pin);

    if (bRawPressed != Button.bLastRawPressed)
    {
        Button.bLastRawPressed = bRawPressed;
        Button.LastChangeTimeMs = millis();
    }

    if ((millis() - Button.LastChangeTimeMs) >= AppConfig::ButtonDebounceMs)
    {
        if (bRawPressed != Button.bStablePressed)
        {
            Button.bStablePressed = bRawPressed;
            if (Button.bStablePressed)
            {
                Button.bPressedEvent = true;
            }
        }
    }
}

bool FDisplayModule::ReadPressed(uint8_t Pin)
{
    const bool bLevel = digitalRead(Pin);
    return AppConfig::ButtonsActiveLow ? !bLevel : bLevel;
}

