#include "Logger.h"

FLogger::FLogger(bool bInEchoToSerial)
    : bEchoToSerial(bInEchoToSerial)
    , Head(0)
    , Count(0)
{
}

void FLogger::Log(ELogLevel Level, const String& Message)
{
    const String Prefix = GetPrefix(Level);
    const String FinalLine = Prefix + Message;

    if (bEchoToSerial)
    {
        Serial.println(FinalLine);
        Serial.println();
    }

    Lines[Head] = FinalLine;
    Head = (Head + 1) % MaxLines;
    if (Count < MaxLines)
    {
        ++Count;
    }
}

void FLogger::Clear()
{
    for (uint8_t Index = 0; Index < MaxLines; ++Index)
    {
        Lines[Index] = "";
    }
    Head = 0;
    Count = 0;
}

String FLogger::GetNewest(uint8_t IndexFromNewest)
{
    if (IndexFromNewest >= Count)
    {
        return "";
    }

    int Slot = static_cast<int>(Head) - 1 - static_cast<int>(IndexFromNewest);
    while (Slot < 0)
    {
        Slot += MaxLines;
    }
    return Lines[Slot];
}

uint8_t FLogger::GetCount()
{
    return Count;
}

String FLogger::GetPrefix(ELogLevel Level)
{
    switch (Level)
    {
        case ELogLevel::Info:    return "[INFO] ";
        case ELogLevel::Warning: return "[WARN] ";
        case ELogLevel::Error:   return "[ERR ] ";
        default:                 return "[....] ";
    }
}
