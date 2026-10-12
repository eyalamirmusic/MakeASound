#pragma once

#include "MidiSender.h"
#include "Settings.h"
#include "StandaloneProcessor.h"
#include "../Core/Description.h"
#include "../Core/Editor.h"
#include "../Host/PluginWrapper.h"
#include "../../Devices/DeviceManager.h"
#include "../../Devices/Engine.h"
#include "../../MIDI/MidiManager.h"

#include <optional>
#include <string>

namespace MakeASound::Standalone
{

// A plugin run without a host and without a window: the devices, the MIDI ports,
// the wrapper, Engine and the settings file. The default app is one client of it;
// a plugin's own app is another, putting whatever UI it likes on top.
//
// Public API. A plugin's own app (makeasound_add_plugin's STANDALONE_SOURCES)
// includes <MakeASound/Plugin/Standalone/MakeASoundStandalone.h>, which adds the
// default app's pieces it may reuse; code that wants no window includes this
// header alone and links MakeASoundStandaloneCore. The smallest app is a class
// holding a StandaloneHost first and its UI after it, calling start() in its
// constructor, run by eacp::Apps::run from the app's own main().
//
// Construction loads the plugin's session from the settings file; start() then
// opens the saved MIDI ports and the stream. Every change made through the host
// (a config, a MIDI port, a reset) is saved at once, and the destructor saves
// once more. The settings file holds the stream config, the open MIDI ports by
// name and the plugin's Session document.
//
// Message thread only, construction and destruction included: PluginWrapper
// asserts it. A UI built over the host must be destroyed before it, so the editor
// goes first, then the stream (Engine stops it), then the wrapper.
class StandaloneHost
{
public:
    // The module's first plugin, its settings at settingsPath(vendor, name).
    StandaloneHost();
    explicit StandaloneHost(const ModuleDescription& module);

    // Any plugin, its settings at settingsFile; an empty path reads and writes
    // no file.
    StandaloneHost(OwningPointer<Plugin> plugin, std::string settingsFile);

    ~StandaloneHost();

    StandaloneHost(const StandaloneHost&) = delete;
    StandaloneHost& operator=(const StandaloneHost&) = delete;

    // Opens the saved MIDI ports by name and starts the stream on the saved
    // config re-pointed at today's devices (resolveConfig), or on the fallback.
    // A failure is printed to stderr and the host keeps what it has. Call it
    // before anything below.
    void start();

    // Restarts the stream on config. The engine may settle on something else
    // (a rate the device lacks), so a UI shows config() afterwards.
    Error applyConfig(const StreamConfig& config);
    const StreamConfig& config() const noexcept { return audioEngine.getConfig(); }

    // The default output device and nothing else: a default microphone into the
    // default speakers is a feedback loop, so an input is only what a user picks.
    const StreamConfig& fallbackConfig() const noexcept { return fallback; }

    Error setMidiInputOpen(int portId, bool open);
    Vector<int> openMidiInputs() const { return midiManager.getOpenInputPorts(); }

    // Nullopt closes the output. MidiManager cannot say which one is open, so the
    // host remembers.
    Error setMidiOutput(std::optional<int> portId);
    std::optional<int> midiOutput() const noexcept { return openOutput; }

    // Lands at offset 0 of the next block, after that block's hardware MIDI.
    // Message thread (any single non-audio thread); false when the queue is full.
    bool injectMidi(const MIDI::Event& event) noexcept;

    // The plugin's own state captured at construction, loaded as a Preset so
    // session-only parameters stay where they are.
    void resetPlugin();

    // Closes every MIDI port and restarts the stream on fallbackConfig().
    void resetAudioAndMidi();

    // Writes the settings file now; a failure is printed, never thrown.
    void saveSettings();
    const std::string& settingsFile() const noexcept { return settingsPathName; }

    // The plugin's editor, or null when it has none; a UI then shows its own
    // (the default app shows GenericEditor).
    OwningPointer<Editor> createEditor() { return plugin().createEditor(); }

    Plugin& plugin() noexcept { return pluginWrapper.plugin(); }
    PluginWrapper& wrapper() noexcept { return pluginWrapper; }
    const BusLayout& busLayout() const noexcept { return pluginWrapper.busLayout(); }
    bool hasMidiInput() const noexcept { return !busLayout().midiInputs.empty(); }
    bool hasMidiOutput() const noexcept { return !busLayout().midiOutputs.empty(); }

    // For a UI that lists devices and ports; change them through the host.
    DeviceManager& devices() noexcept { return deviceManager; }
    MidiManager& midi() noexcept { return midiManager; }
    Engine& engine() noexcept { return audioEngine; }

private:
    static OwningPointer<Plugin> createFirstPlugin(const ModuleDescription& module);

    void restoreSession();
    void restoreMidi(const Settings& settings);
    Error startEngine(const StreamConfig& config);
    Error openMidiOutput(std::optional<int> portId);
    void saveSettingsUnchecked();

    // The stream and the ports as they are now, with no plugin state. Before
    // start() a save keeps the file's own instead.
    Settings currentSettings() const;

    DeviceManager deviceManager;
    MidiManager midiManager;
    MidiSender sender {midiManager};
    PluginWrapper pluginWrapper;
    StandaloneProcessor processor {pluginWrapper, sender};
    Engine audioEngine {deviceManager, midiManager};

    std::string settingsPathName;
    std::string defaultState;
    StreamConfig fallback;
    std::optional<int> openOutput;

    // What the constructor read, for start(); empty afterwards.
    std::optional<Settings> saved;
};

} // namespace MakeASound::Standalone
