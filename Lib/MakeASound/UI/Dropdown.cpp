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

namespace
{
constexpr auto channelIdShift = 256;

DropdownInfo makeChannelDropdown(int channels, int firstChannel, int count)
{
    auto info = DropdownInfo {};
    info.currentId = encodeChannelSelection(firstChannel, count);

    auto label = [](int ch) { return std::to_string(ch + 1); };

    for (auto c = 0; c + 1 < channels; c += 2)
    {
        info.items.create(encodeChannelSelection(c, 1), label(c));
        info.items.create(encodeChannelSelection(c + 1, 1), label(c + 1));
        info.items.create(encodeChannelSelection(c, 2),
                          label(c) + "/" + label(c + 1));
    }

    if (channels % 2 == 1)
        info.items.create(encodeChannelSelection(channels - 1, 1),
                          label(channels - 1));

    return info;
}
} // namespace

int encodeChannelSelection(int firstChannel, int count)
{
    return firstChannel * channelIdShift + count;
}

ChannelSelection decodeChannelSelection(int encoded)
{
    return {encoded / channelIdShift, encoded % channelIdShift};
}

DropdownInfo makeInputChannelDropdown(const DeviceInfo& device,
                                      int firstChannel,
                                      int count)
{
    return makeChannelDropdown(device.inputChannels, firstChannel, count);
}

DropdownInfo makeOutputChannelDropdown(const DeviceInfo& device,
                                       int firstChannel,
                                       int count)
{
    return makeChannelDropdown(device.outputChannels, firstChannel, count);
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
