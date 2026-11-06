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
// once more. The settings file holds the stream config, the MIDI ports the user
// picked by name (a virtual output by the name it was opened under), the
// plugin's Session document, the input mute, the app's window position and the
// app's own keys.
//
// A picked MIDI port stays picked while it is unplugged: it stays in the file,
// and poll(), which an app calls on a timer of its own, opens it again once it
// is back. A start() that opens a saved stream marks the file first
// (Settings::audioOpenPending) and clears the mark once the open returned; a
// host constructed over a file still marked drops its saved stream, so a
// driver that hung the last launch cannot hang this one.
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
    // before anything below. Opening a saved config is guarded: the file says
    // audioOpenPending while the open runs, and is saved again once it
    // returned.
    void start();

    // Whether the constructor found the last launch's open of the saved stream
    // unfinished (audioOpenPending) and dropped that stream, so start() opens
    // fallbackConfig(). An app may tell the user.
    bool droppedAHungConfig() const noexcept { return droppedHungConfig; }

    // Message thread, on a timer the app keeps (a second or so): restarts the
    // stream when Engine asked for it (Engine::takeRestartRequest()), closes the
    // open MIDI inputs and output that are no longer plugged in, and opens
    // every picked port that is plugged in and not open, by name. True when it
    // restarted, opened or closed anything, so a UI showing the ports
    // refreshes. The ports are left alone before start(); the restart is
    // not, so an app that only ever calls applyConfig() polls too.
    //
    // The plugin's prepare() runs here and in the calls that start the stream,
    // never on the audio thread. A device that re-opens on its own in another
    // shape (auto-recovery onto a device with another rate or period) leaves
    // the stream silent until this restarts it: an app that keeps the host
    // without the default app's timer calls poll() on one of its own, or the
    // stream stays silent until the app next applies a config.
    bool poll();

    // Restarts the stream on config. The engine may settle on something else
    // (a rate the device lacks), so a UI shows config() afterwards.
    Error applyConfig(const StreamConfig& config);
    const StreamConfig& config() const noexcept { return audioEngine.getConfig(); }

    // The default output device and nothing else: a default microphone into the
    // default speakers is a feedback loop, so an input is only what a user picks.
    const StreamConfig& fallbackConfig() const noexcept { return fallback; }

    // Opening a port picks its name and closing it unpicks it; the picks are
    // what the file keeps (wantedMidiInputs()), plugged in or not.
    Error setMidiInputOpen(int portId, bool open);
    Vector<int> openMidiInputs() const { return midiManager.getOpenInputPorts(); }

    // The names of the inputs the user picked, in the order picked, one entry
    // per port (two controllers of one model are two entries), including any
    // that are unplugged now.
    const Vector<std::string>& wantedMidiInputs() const noexcept
    {
        return wantedInputs;
    }

    // Nullopt closes the output. MidiManager cannot say which one is open, so the
    // host remembers. Replaces a virtual output. The port's name is kept as
    // the pick (wantedMidiOutput()), and nullopt clears it; a port that fails
    // to open is no pick.
    Error setMidiOutput(std::optional<int> portId);
    std::optional<int> midiOutput() const noexcept { return openOutput; }

    // The name of the output the user picked, plugged in or not; empty for
    // none and while a virtual output is open.
    const std::string& wantedMidiOutput() const noexcept { return wantedOutput; }

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

    // While muted, the plugin hears silence in place of the input device, from
    // the next block on, so an effect playing through the speakers cannot feed
    // back through the microphone. The stream keeps its input side open. Saved
    // at once; off for a new file.
    void setInputMuted(bool muted);
    bool inputMuted() const noexcept { return processor.isInputMuted(); }

    // A second of 440 Hz added to every output from the next block, for a
    // settings panel's Test button (SettingsPanelOptions::testButtonLabel).
    // StandaloneProcessor::playTestTone. Any thread.
    void playTestTone() noexcept { processor.playTestTone(); }

    // Where the app's main window is, for the app to put it back next launch.
    // The host only keeps it: nullopt until the app sets one. Saved at once
    // when it changed.
    void setWindowPosition(std::optional<WindowPosition> position);
    const std::optional<WindowPosition>& windowPosition() const noexcept
    {
        return window;
    }

    // Keys the app keeps in the settings file for itself (Settings::app, say
    // the last state file it saved). Nullopt for a key never set; setting
    // nullopt removes it. Saved at once when it changed.
    std::optional<std::string> appSetting(const std::string& key) const;
    void setAppSetting(const std::string& key, std::optional<std::string> value);
    const std::map<std::string, std::string>& appSettings() const noexcept
    {
        return appValues;
    }

    // The plugin's own state captured at construction, loaded as a Preset so
    // session-only parameters stay where they are.
    void resetPlugin();

    // Closes every MIDI port, a virtual output included, forgets the picks and
    // restarts the stream on fallbackConfig(). The input mute, the window and
    // the app's keys stay.
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
    Error openMidiOutput(std::optional<int> portId, bool report = true);
    Error openVirtualMidiOutput(const std::string& name);
    void closeMidiOutput();
    bool openWantedMidiInputs(bool report);
    bool openWantedMidiOutput(bool report);
    void saveSettingsUnchecked();

    // The stream as it is now and the picked ports, with no plugin state.
    // Before start() a save keeps the file's own instead.
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
    Vector<std::string> wantedInputs;
    std::string wantedOutput;

    std::optional<WindowPosition> window;
    std::map<std::string, std::string> appValues;
    bool openPending = false;
    bool droppedHungConfig = false;
    bool started = false;

    // What the constructor read, for start(); empty afterwards.
    std::optional<Settings> saved;
};

} // namespace MakeASound::Standalone
