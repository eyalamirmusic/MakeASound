#pragma once

// The whole of hosting with MakeASound in one class, with no UI: scan this
// machine's plugins out of process into a database, list them, load one and run
// it on the audio device. Main.cpp puts a window on it; the tests drive it
// headless.

#include <MakeASound/MakeASound.h>
#include <MakeASound/Hosting/MakeASoundHosting.h>

#include <functional>
#include <string>

namespace PluginHost
{
using namespace MakeASound;

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

    // From a UI timer: values the processor reported, to the plugin's editor.
    void idle();

    // --- audio ---

    Error startAudio();
    void stopAudio();
    bool isAudioRunning() const;

    // One line for a status bar.
    const std::string& status() const noexcept { return statusText; }

    // Fired when plugins() or status() moved.
    std::function<void()> onChanged;

private:
    void reloadList();
    void setStatus(std::string text);

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

    std::string statusText;
};

} // namespace PluginHost
