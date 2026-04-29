#include "Dropdown.h"

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

DropdownInfo makeMidiPortDropdown(const Vector<MidiPortInfo>& ports,
                                  int currentId,
                                  bool addNoneSentinel)
{
    auto info = DropdownInfo {};
    info.currentId = currentId;

    if (addNoneSentinel)
        info.items.create(-1, std::string {"(none)"});

    for (auto& port: ports)
        info.items.create(port.id, port.name);

    return info;
}

} // namespace MakeASound::UI
