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

struct ToggleListItem
{
    MIRO_REFLECT(id, label, selected)

    int id {};
    std::string label;
    bool selected {};
};

struct ToggleListInfo
{
    MIRO_REFLECT(items)

    Vector<ToggleListItem> items;
};

DropdownInfo makeOutputDeviceDropdown(const Vector<DeviceInfo>& devices,
                                      int currentId);

DropdownInfo makeInputDeviceDropdown(const Vector<DeviceInfo>& devices,
                                     int currentId);

DropdownInfo makeSampleRateDropdown(const DeviceInfo& device, int currentRate);

DropdownInfo makeBlockSizeDropdown(const Vector<int>& sizes, int currentSize);

ToggleListInfo makeMidiPortToggleList(const Vector<MidiPortInfo>& ports,
                                      const Vector<int>& openPortIds);

} // namespace MakeASound::UI
