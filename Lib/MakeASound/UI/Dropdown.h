#pragma once

#include "../Common/Common.h"
#include "../DeviceInfo/DeviceInfo.h"
#include "../MidiInfo/MidiInfo.h"
#include "../MiroEA/MiroEA.h"

#include <Miro/Miro.h>

#include <string>

namespace MakeASound::UI
{

struct DropdownItem
{
    MIRO_REFLECT(id, label)

    int id {};
    std::string label;
};

struct DropdownInfo
{
    MIRO_REFLECT(items, currentId)

    Vector<DropdownItem> items;
    int currentId {};
};

DropdownInfo makeOutputDeviceDropdown(const Vector<DeviceInfo>& devices,
                                      int currentId);

DropdownInfo makeInputDeviceDropdown(const Vector<DeviceInfo>& devices,
                                     int currentId);

DropdownInfo makeSampleRateDropdown(const DeviceInfo& device, int currentRate);

DropdownInfo makeMidiPortDropdown(const Vector<MidiPortInfo>& ports,
                                  int currentId,
                                  bool addNoneSentinel = true);

} // namespace MakeASound::UI
