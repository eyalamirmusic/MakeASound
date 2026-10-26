#pragma once

// The whole of hosting with MakeASound in one class, with no UI: scan this
// machine's plugins out of process into a database, list them, load one and run
// it on the audio devices and MIDI inputs the user picked. Main.cpp puts a window
// on it; the tests drive it headless.

#include <MakeASound/MakeASound.h>
#include <MakeASound/Hosting/MakeASoundHosting.h>
#include <MakeASound/Plugin/Standalone/Settings.h>

#include <functional>
#include <optional>
#include <string>

namespace PluginHost
{
using namespace MakeASound;

// <app support>/MakeASound/PluginHost/settings.json: the app's audio and MIDI
// picks, in the standalone's settings format (Standalone::Settings, with no
// plugin state).
std::string defaultSettingsPath();

// The stream a load starts: the picked config re-pointed at today's devices
// (Standalone::resolveConfig), so a side whose device is gone takes the
// fallback's; with no pick, the fallback itself.
StreamConfig chooseConfig(const std::optional<StreamConfig>& picked,
                          const Vector<DeviceInfo>& devices,
                          const StreamConfig& fallback);

// The MIDI inputs to open: the picked names among today's ports
// (Standalone::resolvePortIds), a missing one skipped; with no pick, every port.
Vector<int> chooseMidiInputs(const std::optional<Vector<std::string>>& picked,
                             const Vector<MidiPortInfo>& ports);

struct HostOptions
{
    // What a scan sweeps, as ScanSessionOptions' defaults have it: this app's
    // own database (Hosting::defaultPluginCachePath()), every format this
    // platform hosts, and this app relaunched as the worker. Its forceRescan is
    // scan()'s argument.
    Hosting::ScanSessionOptions scan;

    // What a loaded plugin shows as its editor; empty, and it shows none.
    Hosting::HostedEditorFactory editors;

    // Whether loading a plugin starts the audio device.
    bool startsAudio = true;

    // Where the audio and MIDI picks are read at construction and written on
    // every change (the app passes defaultSettingsPath()); empty keeps them for
    // this run only.
    std::string settingsFile;
};

class Host
{
public:
    explicit Host(HostOptions optionsToUse = {});
    ~Host();

    // --- scanning (message thread) ---

    // Starts a sweep: only what is new or changed, or everything when forced.
    // A GUI lets the session's timer drive it (autoPoll); a test pumps poll().
    void scan(bool force = false, bool autoPoll = true);
    void poll();
    bool isScanning() const;

    // --- the list ---

    // What the database holds, narrowed by the filter, sorted by name.
    const Vector<Hosting::PluginDescription>& plugins() const noexcept;
    void setFilter(std::string query);

    // --- the loaded plugin (message thread) ---

    // Loads plugins()[index], stopping the audio for the swap. False, with
    // status() saying why, when it cannot be loaded.
    bool load(int index);
    Hosting::HostedPlugin* plugin() noexcept { return loaded.get(); }

    // Its own editor, or null; createEditor() goes through options.editors.
    OwningPointer<Editor> createEditor();

    // From a UI timer: values the processor reported, to the plugin's editor,
    // and the stream restarted when Engine asked for it.
    void idle();

    // --- audio and MIDI (message thread) ---

    // Starts the stream on config(); stopping leaves the MIDI inputs open.
    Error startAudio();
    void stopAudio();
    bool isAudioRunning() const;

    // The stream that runs, or that a load would start: the pick, re-pointed at
    // today's devices, or with none the default output (duplex for a plugin with
    // an input, since a capture side costs a microphone permission).
    StreamConfig config() const;

    // Picks config and saves it; a running stream restarts on it. The engine
    // may settle on something else (a rate the device lacks), so a UI shows
    // config() afterwards.
    Error applyConfig(const StreamConfig& config);

    // Opens the picked MIDI inputs (every input, before anything was picked).
    // The app calls it once at launch; the open ports are the pick from then on.
    void openMidiInputs();

    // Opens or closes one input and saves the open inputs, by name.
    Error setMidiInputOpen(int portId, bool open);

    // Closes every MIDI input, forgets the audio pick and restarts on the
    // default devices.
    void resetAudioAndMidi();

    // For a UI that lists devices and ports; change them through the host.
    DeviceManager& deviceManager() noexcept { return devices; }
    MidiManager& midiManager() noexcept { return midi; }

    // One line for a status bar.
    const std::string& status() const noexcept { return statusText; }

    // Fired when plugins() or status() moved.
    std::function<void()> onChanged;

private:
    void reloadList();
    void setStatus(std::string text);
    StreamConfig fallbackConfig() const;
    void saveSettings();

    HostOptions options;
    Hosting::FormatManager formats;
    Hosting::PluginCache cache;
    OwningPointer<Hosting::ScanSession> session;

    std::string filter;
    Vector<Hosting::PluginDescription> listed;

    DeviceManager devices;
    MidiManager midi;
    Engine engine {devices, midi};
    OwningPointer<Hosting::HostedPlugin> loaded;

    // What the settings file held, or what was picked since; nullopt is the
    // default (no audio pick, every MIDI input).
    std::optional<StreamConfig> pickedConfig;
    std::optional<Vector<std::string>> pickedMidiInputs;

    std::string statusText;
};

} // namespace PluginHost
