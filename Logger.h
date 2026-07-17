#pragma once

#include "Config.h"

class FLogger
{
public:
    static constexpr uint8_t MaxLines = 24;

    explicit FLogger(bool bInEchoToSerial = true);

    void Log(ELogLevel Level, const String& Message);
    void Clear();
    String GetNewest(uint8_t IndexFromNewest);
    uint8_t GetCount();

private:
    static String GetPrefix(ELogLevel Level);

private:
    bool bEchoToSerial;
    String Lines[MaxLines];
    uint8_t Head;
    uint8_t Count;
};
