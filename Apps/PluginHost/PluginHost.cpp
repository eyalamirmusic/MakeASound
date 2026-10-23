#include "PluginHost.h"

namespace PluginHost
{

namespace
{
std::string pathOrDefault(const std::string& path)
{
    return path.empty() ? Hosting::defaultPluginCachePath() : path;
}

std::string portName(int id, const Vector<MidiPortInfo>& ports)
{
    for (const auto& port: ports)
        if (port.id == id)
            return port.name;

    return {};
}
} // namespace

std::string defaultSettingsPath()
{
    return Standalone::settingsPath("MakeASound", "PluginHost");
}

StreamConfig chooseConfig(const std::optional<StreamConfig>& picked,
                          const Vector<DeviceInfo>& devices,
                          const StreamConfig& fallback)
{
    if (!picked)
        return fallback;

    return Standalone::resolveConfig(*picked, devices, fallback);
}

Vector<int> chooseMidiInputs(const std::optional<Vector<std::string>>& picked,
                             const Vector<MidiPortInfo>& ports)
{
    if (picked)
        return Standalone::resolvePortIds(*picked, ports);

    auto ids = Vector<int> {};

    for (const auto& port: ports)
        ids.add(port.id);

    return ids;
}

Host::Host(HostOptions optionsToUse)
    : options(std::move(optionsToUse))
    , cache(pathOrDefault(options.scan.cachePath))
{
    formats.addDefaultFormats();

    // A database shared with other formats stamps each entry by its own rule.
    Hosting::useFormatStamps(cache, formats);
    reloadList();

    // A file at all pins the MIDI inputs, none included; a config with neither
    // side is no audio pick.
    if (options.settingsFile.empty())
        return;

    if (auto saved = Standalone::loadSettings(options.settingsFile))
    {
        if (saved->audio.input || saved->audio.output)
            pickedConfig = saved->audio;

        pickedMidiInputs = saved->midiInputPorts;
    }
}

Host::~Host()
{
    // Before the plugin goes: the device thread may be inside its process().
    stopAudio();
}

void Host::scan(bool force, bool autoPoll)
{
    auto sweep = options.scan;
    sweep.cachePath = cache.path();
    sweep.forceRescan = force;

    session = EA::makeOwned<Hosting::ScanSession>(formats, std::move(sweep));

    session->onTargetFinished =
        [this](int done, int total, const Hosting::ModuleScan& scan)
    {
        setStatus("Scanning " + std::to_string(done) + "/" + std::to_string(total)
                  + ": " + scan.path);
    };

    session->onFinished = [this](const Hosting::ScanSessionSummary& summary)
    {
        reloadList();
        setStatus(std::to_string(listed.size()) + " plugins, "
                  + std::to_string(summary.crashed) + " crashed while scanning");
    };

    session->setAutoPoll(autoPoll);
    setStatus("Scanning...");
    session->start();
}

void Host::poll()
{
    if (session != nullptr)
        session->poll();
}

bool Host::isScanning() const
{
    return session != nullptr && session->isRunning();
}

const Vector<Hosting::PluginDescription>& Host::plugins() const noexcept
{
    return listed;
}

void Host::setFilter(std::string query)
{
    filter = std::move(query);
    reloadList();
}

void Host::reloadList()
{
    cache.load();
    listed = Hosting::filterPlugins(cache.plugins(), filter);

    if (onChanged)
        onChanged();
}

bool Host::load(int index)
{
    if (index < 0 || index >= listed.size())
        return false;

    const auto description = listed[index];

    // The device thread must leave the old plugin before it is destroyed.
    stopAudio();
    loaded = nullptr;

    auto result = formats.load(
        description, {.name = "MakeASound PluginHost", .editors = options.editors});

    if (result.plugin == nullptr)
    {
        setStatus("Could not load " + description.name + ": " + result.error);
        return false;
    }

    loaded = std::move(result.plugin);
    setStatus("Loaded " + description.name);

    if (options.startsAudio)
        startAudio();

    return true;
}

OwningPointer<Editor> Host::createEditor()
{
    return loaded != nullptr ? loaded->createEditor() : OwningPointer<Editor>();
}

void Host::idle()
{
    if (loaded == nullptr)
        return;

    loaded->syncEditorWithProcessor();

    if (loaded->takeLatencyChanged())
        setStatus(std::string(loaded->name()) + " reports "
                  + std::to_string(loaded->latencySamples())
                  + " samples of latency");
}

Error Host::startAudio()
{
    if (loaded == nullptr)
        return Error::NoError;

    auto error = engine.start(config(), *loaded);

    if (error != Error::NoError)
        setStatus("Audio: " + getErrorMessage(error));

    return error;
}

void Host::stopAudio()
{
    engine.stop();
}

bool Host::isAudioRunning() const
{
    return engine.isRunning();
}

StreamConfig Host::fallbackConfig() const
{
    // Duplex only for a plugin with an input: a capture side costs a
    // microphone permission.
    auto wantsInput = loaded != nullptr && !loaded->getBusLayout().inputs.empty();

    return wantsInput ? devices.getDefaultDuplexConfig()
                      : devices.getDefaultOutputConfig();
}

StreamConfig Host::config() const
{
    if (engine.isRunning())
        return engine.getConfig();

    return chooseConfig(pickedConfig, devices.getDevices(), fallbackConfig());
}

Error Host::applyConfig(const StreamConfig& configToUse)
{
    pickedConfig = configToUse;
    auto error = Error::NoError;

    if (engine.isRunning())
    {
        stopAudio();
        error = startAudio();

        // What the engine settled on is the pick.
        if (engine.isRunning())
            pickedConfig = engine.getConfig();
    }

    saveSettings();
    return error;
}

void Host::openMidiInputs()
{
    for (auto id: chooseMidiInputs(pickedMidiInputs, midi.getInputPorts()))
        if (!midi.isInputOpen(id))
            midi.openInput(id);
}

Error Host::setMidiInputOpen(int portId, bool open)
{
    auto error = Error::NoError;

    if (open)
        error = midi.openInput(portId);
    else
        midi.closeInput(portId);

    if (error != Error::NoError)
        setStatus("MIDI: " + getErrorMessage(error));

    // The open ports are the pick from here on, as the standalone saves them.
    auto ports = midi.getInputPorts();
    auto names = Vector<std::string> {};

    for (auto id: midi.getOpenInputPorts())
        if (auto name = portName(id, ports); !name.empty())
            names.add(name);

    pickedMidiInputs = std::move(names);
    saveSettings();
    return error;
}

void Host::resetAudioAndMidi()
{
    midi.closeAllInputs();
    pickedMidiInputs = Vector<std::string> {};
    pickedConfig.reset();

    if (engine.isRunning())
    {
        stopAudio();
        startAudio();
    }

    saveSettings();
}

void Host::saveSettings()
{
    if (options.settingsFile.empty())
        return;

    auto settings = Standalone::Settings {};

    if (pickedConfig)
        settings.audio = *pickedConfig;

    // Before any MIDI pick, the default: every input.
    if (pickedMidiInputs)
        settings.midiInputPorts = *pickedMidiInputs;
    else
        for (const auto& port: midi.getInputPorts())
            settings.midiInputPorts.add(port.name);

    if (!Standalone::saveSettings(options.settingsFile, settings))
        setStatus("Could not save " + options.settingsFile);
}

void Host::setStatus(std::string text)
{
    statusText = std::move(text);

    if (onChanged)
        onChanged();
}

} // namespace PluginHost
