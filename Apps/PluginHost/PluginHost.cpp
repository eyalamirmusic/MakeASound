#include "PluginHost.h"

namespace PluginHost
{

namespace
{
std::string pathOrDefault(const std::string& path)
{
    return path.empty() ? Hosting::defaultPluginCachePath() : path;
}
} // namespace

Host::Host(HostOptions optionsToUse)
    : options(std::move(optionsToUse))
    , cache(pathOrDefault(options.cachePath))
{
    formats.addDefaultFormats();

    // A database shared with other formats stamps each entry by its own rule.
    Hosting::useFormatStamps(cache, formats);
    reloadList();
}

Host::~Host()
{
    // Before the plugin goes: the device thread may be inside its process().
    stopAudio();
}

void Host::scan(bool force, bool autoPoll)
{
    auto sweep = Hosting::ScanSessionOptions();
    sweep.cachePath = cache.path();
    sweep.searchFolders = options.folders;
    sweep.useDefaultFolders = options.useDefaultFolders;
    sweep.formats = options.scanFormats;
    sweep.forceRescan = force;
    sweep.worker = options.worker;
    sweep.timeoutMs = options.scanTimeoutMs;

    session = EA::makeOwned<Hosting::ScanSession>(formats, sweep);

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

    // Duplex only for a plugin with an input: a capture side costs a
    // microphone permission.
    auto wantsInput = !loaded->getBusLayout().inputs.empty();
    auto config = wantsInput ? devices.getDefaultDuplexConfig()
                             : devices.getDefaultOutputConfig();

    // Every MIDI input, for a plugin that takes MIDI.
    if (!loaded->getBusLayout().midiInputs.empty())
        for (const auto& port: midi.getInputPorts())
            midi.openInput(port.id);

    auto error = engine.start(config, *loaded);

    if (error != Error::NoError)
        setStatus("Audio: " + getErrorMessage(error));

    return error;
}

void Host::stopAudio()
{
    engine.stop();
    midi.closeAllInputs();
}

bool Host::isAudioRunning() const
{
    return engine.isRunning();
}

void Host::setStatus(std::string text)
{
    statusText = std::move(text);

    if (onChanged)
        onChanged();
}

} // namespace PluginHost
