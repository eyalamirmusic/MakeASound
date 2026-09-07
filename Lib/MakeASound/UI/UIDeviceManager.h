#pragma once

#include "../Devices/DeviceManager.h"
#include "Dropdown.h"

namespace MakeASound
{

class UIDeviceManager
{
public:
    explicit UIDeviceManager(DeviceManager& managerToUse);

    UI::DropdownInfo makeBackendDropdown() const;
    UI::DropdownInfo makeOutputDeviceDropdown(int currentId) const;
    UI::DropdownInfo makeInputDeviceDropdown(int currentId) const;
    UI::DropdownInfo makeSampleRateDropdown(int currentDeviceId,
                                            int currentRate) const;

    UI::DropdownInfo makeBlockSizeDropdown(int currentDeviceId,
                                           int currentSize) const;

    // Which of the device's channels the stream uses. Ids encode (firstChannel,
    // count) — see UI::decodeChannelSelection.
    UI::DropdownInfo makeOutputChannelDropdown(int currentDeviceId,
                                               int firstChannel,
                                               int count) const;

    UI::DropdownInfo makeInputChannelDropdown(int currentDeviceId,
                                              int firstChannel,
                                              int count) const;

private:
    DeviceManager* manager;
};

} // namespace MakeASound
