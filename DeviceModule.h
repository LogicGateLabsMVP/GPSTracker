#pragma once

class IDeviceModule
{
public:
    virtual ~IDeviceModule() = default;
    virtual bool Initialize() = 0;
    virtual void Update() = 0;
    virtual void Deinitialize() {}
};
