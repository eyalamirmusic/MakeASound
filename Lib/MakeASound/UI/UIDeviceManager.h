#pragma once

#include "../DeviceManager/DeviceManager.h"
#include "Dropdown.h"

namespace MakeASound
{

class UIDeviceManager
{
public:
    explicit UIDeviceManager(DeviceManager& managerToUse);

    UI::DropdownInfo makeOutputDeviceDropdown(int currentId) const;
    UI::DropdownInfo makeInputDeviceDropdown(int currentId) const;
    UI::DropdownInfo makeSampleRateDropdown(int currentDeviceId,
                                            int currentRate) const;

    UI::DropdownInfo makeBlockSizeDropdown(int currentDeviceId,
                                           int currentSize) const;

private:
    DeviceManager* manager;
};

} // namespace MakeASound
