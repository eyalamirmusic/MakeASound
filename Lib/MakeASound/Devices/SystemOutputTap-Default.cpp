#include "SystemOutputTap.h"

namespace MakeASound
{

SystemOutputTap::~SystemOutputTap() = default;

Error SystemOutputTap::create()
{
    return Error::NOT_SUPPORTED;
}

void SystemOutputTap::destroy() {}

bool SystemOutputTap::isCreated() const
{
    return false;
}

const std::string& SystemOutputTap::getCaptureDeviceUid() const
{
    return captureDeviceUid;
}

} // namespace MakeASound
