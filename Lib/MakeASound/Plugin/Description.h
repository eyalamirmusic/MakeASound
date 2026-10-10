#pragma once

#include "../Common/Common.h"

#include <cstdint>
#include <functional>
#include <string_view>

namespace MakeASound
{

class Plugin;

// Four characters, the identity AU uses natively. VST3's 128-bit UID is derived
// from the module's manufacturerCode plus the plugin's pluginCode, so a stable
// four-character code is all anyone writes: `.pluginCode = "Gain"`.
struct FourCC
{
    constexpr FourCC() = default;

    constexpr FourCC(const char (&s)[5])
        : chars {s[0], s[1], s[2], s[3]}
    {
    }

    constexpr uint32_t toUint32() const noexcept
    {
        return (uint32_t(static_cast<unsigned char>(chars[0])) << 24)
               | (uint32_t(static_cast<unsigned char>(chars[1])) << 16)
               | (uint32_t(static_cast<unsigned char>(chars[2])) << 8)
               | uint32_t(static_cast<unsigned char>(chars[3]));
    }

    constexpr bool operator==(const FourCC&) const = default;

    char chars[4] {};
};

enum class Category
{
    Effect,
    Instrument,

    // Declares MIDI in and out plus at least one audio output bus: hosts drive
    // MIDI processing through the render call. AU maps this to 'aumi'; VST3 has
    // no such class, so it ships as an instrument.
    MidiEffect
};

// The VST3 subcategory string a category implies when the description names
// none: Effect -> "Fx", Instrument -> "Instrument|Synth".
std::string_view defaultSubcategory(Category category) noexcept;

using PluginCreateFn = std::function<OwningPointer<Plugin>()>;

// One plugin class within a module.
struct PluginDescription
{
    std::string_view name;
    std::string_view version = "1.0.0";
    Category category = Category::Effect;
    std::string_view subcategory = {};

    // Unique per plugin within a manufacturer.
    FourCC pluginCode;

    PluginCreateFn create;
};

// A module may host more than one plugin class: VST3 factories and AU bundles
// both allow it.
struct ModuleDescription
{
    std::string_view vendor;
    std::string_view url = {};
    std::string_view email = {};

    // The bundle's version, distinct from each plugin's own. Bump it whenever
    // `plugins` changes: macOS caches a bundle's AudioComponents list keyed by
    // CFBundleVersion and will not re-read one whose version has not moved.
    std::string_view version = "1.0.0";

    // Unique per vendor. Used natively by AU and mixed into every VST3 UID.
    FourCC manufacturerCode;

    Vector<PluginDescription> plugins;
};

// Implemented exactly once per plugin module, in the plugin's own TU.
ModuleDescription describeModule();

} // namespace MakeASound
