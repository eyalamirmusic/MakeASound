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
// name (a virtual output by the name it was opened under) and the plugin's
// Session document.
//
// With no settings file to read, a port under a JUCE id scheme imports what a
// JUCE build of the plugin's standalone saved, from that build's settings file
// (juceImportPath): its `filterState` goes to the plugin's loadForeignState as
// JuceStandalone, as a Session. Its audio and MIDI setup are not read. The next
// save writes the native file, so the import happens once.
//
// Message thread only, construction and destruction included: PluginWrapper
// asserts it. A UI built over the host must be destroyed before it, so the editor
// goes first, then the stream (Engine stops it), then the wrapper.
class StandaloneHost
{
public:
    // The module's first plugin, its settings at settingsPath(vendor, name), a
    // JUCE build's at juceImportPath() of its description. The only member that
    // needs describeModule(), and in a TU of its own: an app that never calls it
    // defines no module.
    StandaloneHost();
    explicit StandaloneHost(const ModuleDescription& module);

    // Any plugin, its settings at settingsFile; an empty path reads and writes
    // no file. juceSettingsFile, when not empty, is a JUCE build's settings file
    // to import from when settingsFile gives nothing, under any id scheme. The
    // module constructor already handles a renamed port, through juce.pluginName
    // in juceImportPath(); this one is for a plugin not built from a
    // description, or for an explicit path. `identity` is
    // the wrapper's (parameterIdentityFor() of the plugin's description); the
    // standalone has no host ids, so it only keeps the plugin's parameter list
    // as every format sees it.
    StandaloneHost(OwningPointer<Plugin> plugin,
                   std::string settingsFile,
                   std::string juceSettingsFile = {},
                   ParameterIdentity identity = {});

    ~StandaloneHost();

    // The JUCE build's settings file the module constructor imports from:
    // juceSettingsPath() of juce.pluginName, or of the description's name when
    // that is empty. Empty unless the description's identity policy says a JUCE
    // build came before (IdentityPolicy::juceStandaloneImport, the JUCE
    // schemes'): under Native or TamberPlug no JUCE build saved anything.
    static std::string juceImportPath(const PluginDescription& plugin);

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
    // host remembers. Replaces a virtual output.
    Error setMidiOutput(std::optional<int> portId);
    std::optional<int> midiOutput() const noexcept { return openOutput; }

    // Opens a virtual MIDI output named `name`, a port every other app on the
    // machine can subscribe to, and sends the plugin's MIDI out through it, in
    // place of the port setMidiOutput picked (midiOutput() is then nullopt). An
    // empty name closes it. Saved by name, so start() opens it again. An error
    // where the platform has no virtual ports (Windows), and the host then has
    // no output open.
    Error setVirtualMidiOutput(const std::string& name);
    const std::string& virtualMidiOutput() const noexcept { return virtualOutput; }

    // Lands at offset 0 of the next block, after that block's hardware MIDI.
    // Message thread (any single non-audio thread); false when the queue is full.
    bool injectMidi(const MIDI::Event& event) noexcept;

    // The plugin's own state captured at construction, loaded as a Preset so
    // session-only parameters stay where they are.
    void resetPlugin();

    // Closes every MIDI port, a virtual output included, and restarts the stream
    // on fallbackConfig().
    void resetAudioAndMidi();

    // Writes the settings file now; a failure is printed, never thrown.
    void saveSettings();
    const std::string& settingsFile() const noexcept { return settingsPathName; }

    // The JUCE build's settings file the constructor would import from.
    const std::string& juceSettingsFile() const noexcept { return juceSettingsName; }

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
    void importJuceSettings();
    void restoreMidi(const Settings& settings);
    Error startEngine(const StreamConfig& config);
    Error openMidiOutput(std::optional<int> portId);
    Error openVirtualMidiOutput(const std::string& name);
    void closeMidiOutput();
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
    std::string juceSettingsName;
    std::string defaultState;
    StreamConfig fallback;
    std::optional<int> openOutput;
    std::string virtualOutput;

    // What the constructor read, for start(); empty afterwards.
    std::optional<Settings> saved;
};

} // namespace MakeASound::Standalone
