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

DropdownInfo makeOutputDeviceDropdown(const Vector<DeviceInfo>& devices,
                                      int currentId);

DropdownInfo makeInputDeviceDropdown(const Vector<DeviceInfo>& devices,
                                     int currentId);

DropdownInfo makeSampleRateDropdown(const DeviceInfo& device, int currentRate);

DropdownInfo makeBlockSizeDropdown(const Vector<int>& sizes, int currentSize);

// A channel-selection dropdown packs (firstChannel, count) into a single int id
// so it rides the existing DropdownInfo. Use these to encode the item ids and
// to decode a selected id back into a slice.
struct ChannelSelection
{
    int firstChannel {};
    int count {};
};

int encodeChannelSelection(int firstChannel, int count);
ChannelSelection decodeChannelSelection(int encoded);

// Lists the selectable inputs/outputs of a device as single channels and stereo
// pairs (1, 2, 1/2, 3, 4, 3/4, ...), so a specific input/output can be picked
// out of a multi-channel device. currentId is the encoded current slice.
DropdownInfo makeInputChannelDropdown(const DeviceInfo& device,
                                      int firstChannel,
                                      int count);

DropdownInfo makeOutputChannelDropdown(const DeviceInfo& device,
                                       int firstChannel,
                                       int count);

ToggleListInfo makeMidiPortToggleList(const Vector<MidiPortInfo>& ports,
                                      const Vector<int>& openPortIds);

} // namespace MakeASound::UI
