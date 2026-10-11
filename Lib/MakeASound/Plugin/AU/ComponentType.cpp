#include "ComponentType.h"
#include "../Core/Plugin.h"

#include <algorithm>
#include <array>
#include <charconv>

namespace MakeASound::AU
{

ComponentInfo componentInfoFor(const ModuleDescription& module,
                               const PluginDescription& plugin)
{
    auto created = plugin.create ? plugin.create() : OwningPointer<Plugin> {};
    auto layout = created ? created->getBusLayout() : BusLayout {};

    auto info = ComponentInfo {.subtype = plugin.pluginCode.toUint32(),
                               .manufacturer = module.manufacturerCode.toUint32(),
                               .numInputBuses = layout.inputs.size(),
                               .numOutputBuses = layout.outputs.size()};

    if (plugin.category == Category::Instrument)
        info.type = kAudioUnitType_MusicDevice;
    else if (plugin.category == Category::MidiEffect)
        info.type = kAudioUnitType_MIDIProcessor;
    else if (!layout.midiInputs.empty())
        info.type = kAudioUnitType_MusicEffect;
    else
        info.type = kAudioUnitType_Effect;

    return info;
}

const PluginDescription* findPlugin(const ModuleDescription& module,
                                    OSType subtype) noexcept
{
    for (const auto& plugin: module.plugins)
        if (plugin.pluginCode.toUint32() == subtype)
            return &plugin;

    return nullptr;
}

std::string fourCCString(OSType code)
{
    return {static_cast<char>((code >> 24) & 0xff),
            static_cast<char>((code >> 16) & 0xff),
            static_cast<char>((code >> 8) & 0xff),
            static_cast<char>(code & 0xff)};
}

UInt32 versionNumber(std::string_view version) noexcept
{
    constexpr auto limits = std::array<UInt32, 3> {0xffff, 0xff, 0xff};
    auto parts = std::array<UInt32, 3> {};

    for (auto i = 0; i < 3; ++i)
    {
        auto dot = version.find('.');
        auto text = version.substr(0, dot);
        auto value = 0u;

        if (std::from_chars(text.data(), text.data() + text.size(), value).ec
            == std::errc {})
            parts[i] = std::min(value, limits[i]);

        if (dot == std::string_view::npos)
            break;

        version.remove_prefix(dot + 1);
    }

    return parts[0] << 16 | parts[1] << 8 | parts[2];
}

} // namespace MakeASound::AU
