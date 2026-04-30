#include "UIDeviceManager.h"

namespace MakeASound
{

UIDeviceManager::UIDeviceManager(DeviceManager& managerToUse)
    : manager(&managerToUse)
{
}

UI::DropdownInfo UIDeviceManager::makeOutputDeviceDropdown(int currentId) const
{
    return UI::makeOutputDeviceDropdown(manager->getDevices(), currentId);
}

UI::DropdownInfo UIDeviceManager::makeInputDeviceDropdown(int currentId) const
{
    return UI::makeInputDeviceDropdown(manager->getDevices(), currentId);
}

UI::DropdownInfo UIDeviceManager::makeSampleRateDropdown(int currentDeviceId,
                                                         int currentRate) const
{
    for (auto& device: manager->getDevices())
        if (device.id == currentDeviceId)
            return UI::makeSampleRateDropdown(device, currentRate);

    auto info = UI::DropdownInfo {};
    info.currentId = currentRate;
    return info;
}

} // namespace MakeASound
