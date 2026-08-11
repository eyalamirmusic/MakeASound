#pragma once

#include "../Common/Common.h"
#include "../Devices/DeviceInfo.h"
#include "../MIDI/MidiInfo.h"

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

// Item ids are the Backend enumerator's value, so a selected id casts back.
DropdownInfo makeBackendDropdown(const Vector<Backend>& backends, Backend current);

DropdownInfo makeOutputDeviceDropdown(const Vector<DeviceInfo>& devices,
                                      int currentId);

DropdownInfo makeInputDeviceDropdown(const Vector<DeviceInfo>& devices,
                                     int currentId);

DropdownInfo makeSampleRateDropdown(const DeviceInfo& device, int currentRate);

DropdownInfo makeBlockSizeDropdown(const Vector<int>& sizes, int currentSize);

// Channel dropdowns pack (firstChannel, count) into a single int id so they
// ride the existing DropdownInfo.
struct ChannelSelection
{
    int firstChannel {};
    int count {};
};

int encodeChannelSelection(int firstChannel, int count);
ChannelSelection decodeChannelSelection(int encoded);

// Lists single channels and stereo pairs: 1, 2, 1/2, 3, 4, 3/4, ...
DropdownInfo makeInputChannelDropdown(const DeviceInfo& device,
                                      int firstChannel,
                                      int count);

DropdownInfo makeOutputChannelDropdown(const DeviceInfo& device,
                                       int firstChannel,
                                       int count);

ToggleListInfo makeMidiPortToggleList(const Vector<MidiPortInfo>& ports,
                                      const Vector<int>& openPortIds);

} // namespace MakeASound::UI
