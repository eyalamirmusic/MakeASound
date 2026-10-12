#include "Settings.h"

#include <eacp/Core/Utils/FilePath.h>
#include <eacp/Core/Utils/Files.h>

#include <algorithm>
#include <cctype>

namespace MakeASound::Standalone
{

namespace
{
bool isBlank(const std::string& text)
{
    return std::ranges::all_of(text,
                               [](unsigned char c) { return std::isspace(c) != 0; });
}

const DeviceInfo* findDevice(const Vector<DeviceInfo>& devices,
                             const std::string& name,
                             bool input)
{
    for (const auto& device: devices)
        if (device.name == name && device.hasChannels(input))
            return &device;

    return nullptr;
}

std::optional<StreamParameters>
    resolveSide(const std::optional<StreamParameters>& saved,
                const Vector<DeviceInfo>& devices,
                const std::optional<StreamParameters>& fallback,
                bool input)
{
    if (!saved)
        return std::nullopt;

    const auto* device = findDevice(devices, saved->device.name, input);

    if (device == nullptr)
        return fallback;

    auto available = input ? device->inputChannels : device->outputChannels;
    auto count = std::clamp(saved->nChannels, 1, available);
    auto first = std::clamp(saved->firstChannel, 0, available - count);

    return StreamParameters(*device, input, count, first);
}

bool offersRate(const std::optional<StreamParameters>& side, int rate)
{
    return !side || side->device.sampleRates.empty()
           || deviceSupportsSampleRate(side->device, rate);
}
} // namespace

std::string settingsPath(std::string_view vendor, std::string_view pluginName)
{
    auto directory = eacp::FilePath::appSupportDirectory(vendor, pluginName);
    return (directory / "settings.json").str();
}

std::optional<Settings> loadSettings(const std::string& path)
{
    try
    {
        auto text = eacp::Files::readFile(path);

        if (isBlank(text))
            return std::nullopt;

        auto json = Miro::Json::getParsedValue(text);

        if (!json.isObject())
            return std::nullopt;

        return Miro::createFromJSON<Settings>(json);
    }
    catch (...)
    {
        return std::nullopt;
    }
}

bool saveSettings(const std::string& path, const Settings& settings)
{
    try
    {
        auto text = Miro::toJSONString(settings, 2);
        auto bytes = Span<const std::uint8_t>(
            reinterpret_cast<const std::uint8_t*>(text.data()), text.size());

        eacp::Files::writeFileAtomically(path, bytes);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

StreamConfig resolveConfig(const StreamConfig& saved,
                           const Vector<DeviceInfo>& devices,
                           const StreamConfig& fallback)
{
    if (!saved.input && !saved.output)
        return fallback;

    auto resolved = saved;
    resolved.input = resolveSide(saved.input, devices, fallback.input, true);
    resolved.output = resolveSide(saved.output, devices, fallback.output, false);

    if (resolved.maxBlockSize <= 0)
        resolved.maxBlockSize = fallback.maxBlockSize;

    if (!resolved.input && !resolved.output)
    {
        resolved.sampleRate = fallback.sampleRate;
        return resolved;
    }

    if (saved.sampleRate > 0 && offersRate(resolved.input, saved.sampleRate)
        && offersRate(resolved.output, saved.sampleRate))
        return resolved;

    auto deviceOf = [](const std::optional<StreamParameters>& side)
    { return side ? side->device : DeviceInfo {}; };

    resolved.sampleRate = pickCompatibleSampleRate(deviceOf(resolved.output),
                                                   deviceOf(resolved.input));

    return resolved;
}

Vector<int> resolvePortIds(const Vector<std::string>& names,
                           const Vector<MidiPortInfo>& ports)
{
    auto ids = Vector<int> {};

    // Two controllers of one model share a name; each saved entry takes the
    // next port of that name not already taken.
    for (const auto& name: names)
    {
        auto taken = [&](const MidiPortInfo& port)
        { return std::ranges::find(ids, port.id) != ids.end(); };

        auto found =
            std::ranges::find_if(ports,
                                 [&](const MidiPortInfo& port)
                                 { return port.name == name && !taken(port); });

        if (found != ports.end())
            ids.add(found->id);
    }

    return ids;
}

} // namespace MakeASound::Standalone
