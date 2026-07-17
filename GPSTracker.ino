#include "Tracker.h"

FTrackerDevice GDevice;

void setup()
{
    GDevice.Initialize();
}

void loop()
{
    GDevice.Update();
}
