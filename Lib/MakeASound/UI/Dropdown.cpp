#include "Dropdown.h"

#include <algorithm>

namespace MakeASound::UI
{
namespace
{
DropdownInfo makeDeviceDropdown(const Vector<DeviceInfo>& devices,
                                int currentId,
                                bool wantOutput)
{
    auto info = DropdownInfo {};
    info.currentId = currentId;

    for (auto& device: devices)
    {
        auto channels = wantOutput ? device.outputChannels : device.inputChannels;

        if (channels == 0)
            continue;

        info.items.create(device.id, device.name);
    }

    return info;
}
} // namespace

DropdownInfo makeOutputDeviceDropdown(const Vector<DeviceInfo>& devices,
                                      int currentId)
{
    return makeDeviceDropdown(devices, currentId, true);
}

DropdownInfo makeInputDeviceDropdown(const Vector<DeviceInfo>& devices,
                                     int currentId)
{
    return makeDeviceDropdown(devices, currentId, false);
}

DropdownInfo makeSampleRateDropdown(const DeviceInfo& device, int currentRate)
{
    auto info = DropdownInfo {};
    info.currentId = currentRate;

    for (auto rate: device.sampleRates)
        info.items.create(rate, std::to_string(rate) + " Hz");

    return info;
}

DropdownInfo makeBlockSizeDropdown(const Vector<int>& sizes, int currentSize)
{
    auto info = DropdownInfo {};
    info.currentId = currentSize;

    for (auto size: sizes)
        info.items.create(size, std::to_string(size));

    return info;
}

ToggleListInfo makeMidiPortToggleList(const Vector<MidiPortInfo>& ports,
                                      const Vector<int>& openPortIds)
{
    auto info = ToggleListInfo {};

    for (auto& port: ports)
    {
        auto selected = std::ranges::find(openPortIds, port.id) != openPortIds.end();
        info.items.create(port.id, port.name, selected);
    }

    return info;
}

} // namespace MakeASound::UI
