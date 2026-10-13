#pragma once

#include "../../Devices/DeviceInfo.h"
#include "../../MIDI/MidiInfo.h"

#include <optional>
#include <string>
#include <string_view>

namespace MakeASound::Standalone
{

struct Settings
{
    int version = 1;
    StreamConfig audio;

    // Names, not ids: ids are handed out per launch.
    Vector<std::string> midiInputPorts;
    std::string midiOutputPort;

    // The plugin's Session document.
    std::string pluginState;

    MIRO_REFLECT(version, audio, midiInputPorts, midiOutputPort, pluginState)
};

// <app support>/<vendor>/<plugin>/settings.json
std::string settingsPath(std::string_view vendor, std::string_view pluginName);

// Where a JUCE build's standalone named `juceName` (its JucePlugin_Name) kept its
// settings, a PropertiesFile whose `filterState` is the plugin's state
// (juce_audio_plugin_client_Standalone.cpp): `~/Library/Application
// Support/<Name>.settings` on macOS and iOS, `%APPDATA%\<Name>\<Name>.settings`
// on Windows, `~/.config/<Name>.settings` on Linux. Empty for an empty name and
// on any other platform.
std::string juceSettingsPath(std::string_view juceName);

// Nullopt when the file is missing, blank or not a JSON object.
std::optional<Settings> loadSettings(const std::string& path);

// Atomic, creating the directory; false on failure, never throws.
bool saveSettings(const std::string& path, const Settings& settings);

// A saved config names devices from an earlier launch. Each side is re-pointed at
// the device of that name present now, keeping its channel span clamped to what
// the device has; a side whose device is gone takes the fallback's side. The
// sample rate is kept when every device lists it, and otherwise chosen as
// DeviceManager chooses a default one. The block size is kept. A config with
// neither side is the fallback.
StreamConfig resolveConfig(const StreamConfig& saved,
                           const Vector<DeviceInfo>& devices,
                           const StreamConfig& fallback);

// The ports present now under the saved names, in order; a missing name is
// skipped.
Vector<int> resolvePortIds(const Vector<std::string>& names,
                           const Vector<MidiPortInfo>& ports);

} // namespace MakeASound::Standalone
