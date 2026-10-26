#include "StandaloneHost.h"
#include "../Compat/JuceState.h"
#include "../Core/Identity.h"

#include <eacp/Core/Utils/Files.h>

#include <algorithm>
#include <cassert>
#include <iostream>

namespace MakeASound::Standalone
{

namespace
{
Error reportIfFailed(Error error)
{
    if (error != Error::NoError)
        std::cerr << getErrorMessage(error) << '\n';

    return error;
}

std::optional<int> findPort(const std::string& name,
                            const Vector<MidiPortInfo>& ports)
{
    if (name.empty())
        return {};

    auto ids = resolvePortIds({name}, ports);

    if (ids.empty())
        return {};

    return ids[0];
}

std::string portName(int id, const Vector<MidiPortInfo>& ports)
{
    for (auto& port: ports)
        if (port.id == id)
            return port.name;

    return {};
}

bool isPresent(int id, const Vector<MidiPortInfo>& ports)
{
    return std::ranges::any_of(
        ports, [id](const MidiPortInfo& port) { return port.id == id; });
}

bool hasSide(const StreamConfig& config)
{
    return config.input.has_value() || config.output.has_value();
}

// The standalone is its own host: the plugin's name, or "Standalone".
std::string standaloneHostName(const Plugin& plugin)
{
    auto name = plugin.name();
    return name.empty() ? std::string("Standalone") : std::string(name);
}
} // namespace

OwningPointer<Plugin>
    StandaloneHost::createFirstPlugin(const ModuleDescription& module)
{
    assert(!module.plugins.empty() && "describeModule() names no plugin");
    setCurrentPluginFormat(PluginFormat::Standalone);
    return module.plugins[0].create();
}

StandaloneHost::StandaloneHost(const ModuleDescription& module)
    : pluginWrapper(createFirstPlugin(module),
                    PluginFormat::Standalone,
                    parameterIdentityFor(module.plugins[0]))
    , settingsPathName(settingsPath(module.vendor, pluginWrapper.plugin().name()))
    , juceSettingsName(juceImportPath(module.plugins[0]))
{
    pluginWrapper.setHostName(standaloneHostName(pluginWrapper.plugin()));
    restoreSession();
}

std::string StandaloneHost::juceImportPath(const PluginDescription& plugin)
{
    if (!identityPolicyFor(plugin).juceStandaloneImport)
        return {};

    auto name =
        plugin.juce.pluginName.empty() ? plugin.name : plugin.juce.pluginName;
    return juceSettingsPath(name);
}

StandaloneHost::StandaloneHost(OwningPointer<Plugin> plugin,
                               std::string settingsFile,
                               std::string juceSettingsFile,
                               ParameterIdentity identity)
    : pluginWrapper(std::move(plugin), PluginFormat::Standalone, identity)
    , settingsPathName(std::move(settingsFile))
    , juceSettingsName(std::move(juceSettingsFile))
{
    // The plugin came already built; from here on, the module is a standalone.
    setCurrentPluginFormat(PluginFormat::Standalone);
    pluginWrapper.setHostName(standaloneHostName(pluginWrapper.plugin()));
    restoreSession();
}

StandaloneHost::~StandaloneHost()
{
    saveSettings();
}

void StandaloneHost::restoreSession()
{
    // A preset, so resetting leaves session-only parameters where they are.
    defaultState = pluginWrapper.saveState(StateContext::Preset);

    if (!settingsPathName.empty())
        saved = loadSettings(settingsPathName);

    if (saved)
    {
        processor.setInputMuted(saved->inputMuted);
        window = saved->window;
        appValues = saved->app;

        // The last launch never came back from opening this stream: start()
        // takes the fallback, and the next save writes the file without it.
        if (saved->audioOpenPending)
        {
            saved->audio = {};
            saved->audioOpenPending = false;
            droppedHungConfig = true;
        }
    }

    if (saved && !saved->pluginState.empty())
        pluginWrapper.loadState(saved->pluginState, StateContext::Session);
    else if (!saved)
        importJuceSettings();

    fallback = deviceManager.getDefaultOutputConfig();
}

void StandaloneHost::importJuceSettings()
{
    if (juceSettingsName.empty())
        return;

    try
    {
        auto state =
            Compat::juceStandaloneState(eacp::Files::readFile(juceSettingsName));

        if (!state)
            return;

        auto foreign = ForeignState {};
        foreign.origin = ForeignOrigin::JuceStandalone;
        foreign.bytes = *state;
        foreign.context = StateContext::Session;
        pluginWrapper.loadForeignState(foreign);
    }
    catch (...)
    {
    }
}

void StandaloneHost::start()
{
    if (saved)
        restoreMidi(*saved);

    // A driver can hang inside the open; the mark on disk is what lets the
    // next launch tell. The fallback is not guarded: it is what a guarded
    // launch falls back to.
    auto guarded = saved && hasSide(saved->audio);

    if (guarded)
    {
        openPending = true;
        saveSettings();
    }

    startEngine(
        saved ? resolveConfig(saved->audio, deviceManager.getDevices(), fallback)
              : fallback);
    saved.reset();
    started = true;

    if (guarded)
    {
        openPending = false;
        saveSettings();
    }
}

void StandaloneHost::restoreMidi(const Settings& settings)
{
    if (hasMidiInput())
    {
        wantedInputs = settings.midiInputPorts;
        openWantedMidiInputs(true);
    }

    if (!hasMidiOutput())
        return;

    if (!settings.midiVirtualOutput.empty())
    {
        openVirtualMidiOutput(settings.midiVirtualOutput);
        return;
    }

    wantedOutput = settings.midiOutputPort;
    openWantedMidiOutput(true);
}

bool StandaloneHost::poll()
{
    auto restarted = audioEngine.takeRestartRequest();

    if (restarted)
        reportIfFailed(audioEngine.restart());

    if (!started)
        return restarted;

    auto inputs = openWantedMidiInputs(false);
    auto output = openWantedMidiOutput(false);
    return restarted || inputs || output;
}

bool StandaloneHost::openWantedMidiInputs(bool report)
{
    if (!hasMidiInput())
        return false;

    auto ports = midiManager.getInputPorts();
    auto changed = false;

    // A port that went away leaves its connection to an endpoint that may
    // never come back (a virtual source is made anew); its pick opens the
    // port of that name again once there is one.
    for (auto id: midiManager.getOpenInputPorts())
    {
        if (id >= 0 && !isPresent(id, ports))
        {
            midiManager.closeInput(id);
            changed = true;
        }
    }

    // Each pick is one port: first an open port of its name, then a closed
    // one, which it opens. Two controllers of one model are two picks.
    auto claimed = Vector<int> {};
    auto unmatched = Vector<std::string> {};

    auto unclaimedPort = [&](const std::string& name, bool open)
    {
        auto matches = [&](const MidiPortInfo& port)
        {
            return port.name == name && !claimed.contains(port.id)
                   && midiManager.isInputOpen(port.id) == open;
        };

        return std::ranges::find_if(ports, matches);
    };

    for (const auto& name: wantedInputs)
    {
        if (auto found = unclaimedPort(name, true); found != ports.end())
            claimed.add(found->id);
        else
            unmatched.add(name);
    }

    for (const auto& name: unmatched)
    {
        auto found = unclaimedPort(name, false);

        if (found == ports.end())
            continue;

        claimed.add(found->id);
        auto error = midiManager.openInput(found->id);

        if (report)
            reportIfFailed(error);

        changed = changed || error == Error::NoError;
    }

    return changed;
}

bool StandaloneHost::openWantedMidiOutput(bool report)
{
    if (!hasMidiOutput() || !virtualOutput.empty() || wantedOutput.empty())
        return false;

    auto ports = midiManager.getOutputPorts();

    if (openOutput && isPresent(*openOutput, ports))
        return false;

    // Gone, or never opened: either way the pick reopens by name.
    auto hadOutput = openOutput.has_value();

    if (auto id = findPort(wantedOutput, ports))
        return openMidiOutput(id, report) == Error::NoError || hadOutput;

    closeMidiOutput();
    return hadOutput;
}

Error StandaloneHost::startEngine(const StreamConfig& config)
{
    return reportIfFailed(audioEngine.start(config, processor));
}

Error StandaloneHost::applyConfig(const StreamConfig& config)
{
    auto error = startEngine(config);
    saveSettings();
    return error;
}

Error StandaloneHost::setMidiInputOpen(int portId, bool open)
{
    auto error = Error::NoError;
    auto wasOpen = midiManager.isInputOpen(portId);
    auto name = portName(portId, midiManager.getInputPorts());

    if (open)
        error = reportIfFailed(midiManager.openInput(portId));
    else
        midiManager.closeInput(portId);

    // Reopening an open port that then fails closes it, so what changed is
    // whether it is open now.
    auto isOpen = midiManager.isInputOpen(portId);

    if (!name.empty() && isOpen && !wasOpen)
        wantedInputs.add(name);

    if (!name.empty() && wasOpen && !isOpen)
        if (auto found = std::ranges::find(wantedInputs, name);
            found != wantedInputs.end())
            wantedInputs.erase(found);

    saveSettings();
    return error;
}

Error StandaloneHost::setMidiOutput(std::optional<int> portId)
{
    auto error = openMidiOutput(portId);

    wantedOutput = openOutput ? portName(*openOutput, midiManager.getOutputPorts())
                              : std::string {};
    saveSettings();
    return error;
}

Error StandaloneHost::setVirtualMidiOutput(const std::string& name)
{
    auto error = openVirtualMidiOutput(name);

    // It replaces the picked port, so the pick goes with it.
    wantedOutput.clear();
    saveSettings();
    return error;
}

void StandaloneHost::closeMidiOutput()
{
    sender.stop();
    midiManager.closeOutput();
    openOutput.reset();
    virtualOutput.clear();
}

Error StandaloneHost::openVirtualMidiOutput(const std::string& name)
{
    closeMidiOutput();

    if (name.empty())
        return Error::NoError;

    auto error = reportIfFailed(midiManager.openVirtualOutput(name));

    if (error != Error::NoError)
        return error;

    virtualOutput = name;
    sender.start();
    return error;
}

Error StandaloneHost::openMidiOutput(std::optional<int> portId, bool report)
{
    closeMidiOutput();

    if (!portId)
        return Error::NoError;

    auto error = midiManager.openOutput(*portId);

    if (report)
        reportIfFailed(error);

    if (error != Error::NoError)
        return error;

    openOutput = portId;
    sender.start();
    return error;
}

bool StandaloneHost::injectMidi(const MIDI::Event& event) noexcept
{
    return processor.injectMidi(event);
}

void StandaloneHost::setInputMuted(bool muted)
{
    processor.setInputMuted(muted);
    saveSettings();
}

void StandaloneHost::setWindowPosition(std::optional<WindowPosition> position)
{
    if (position == window)
        return;

    window = position;
    saveSettings();
}

std::optional<std::string> StandaloneHost::appSetting(const std::string& key) const
{
    if (auto found = appValues.find(key); found != appValues.end())
        return found->second;

    return std::nullopt;
}

void StandaloneHost::setAppSetting(const std::string& key,
                                   std::optional<std::string> value)
{
    if (appSetting(key) == value)
        return;

    if (value)
        appValues[key] = std::move(*value);
    else
        appValues.erase(key);

    saveSettings();
}

void StandaloneHost::resetPlugin()
{
    pluginWrapper.loadState(defaultState, StateContext::Preset);
    saveSettings();
}

void StandaloneHost::resetAudioAndMidi()
{
    for (auto id: midiManager.getOpenInputPorts())
        midiManager.closeInput(id);

    openMidiOutput(std::nullopt);
    wantedInputs.clear();
    wantedOutput.clear();
    startEngine(fallback);
    saveSettings();
}

void StandaloneHost::saveSettings()
{
    if (settingsPathName.empty())
        return;

    // Reached from the destructor and from event-loop callbacks, where a
    // throwing saveState has nowhere to go.
    try
    {
        saveSettingsUnchecked();
    }
    catch (const std::exception& e)
    {
        std::cerr << "Could not save " << settingsPathName << ": " << e.what()
                  << '\n';
    }
}

void StandaloneHost::saveSettingsUnchecked()
{
    // Before start() the devices and ports are the file's; what the host owns
    // whether started or not is its own.
    auto settings = saved ? *saved : currentSettings();
    settings.pluginState = pluginWrapper.saveState(StateContext::Session);
    settings.inputMuted = processor.isInputMuted();
    settings.window = window;
    settings.app = appValues;
    settings.audioOpenPending = openPending;

    if (!Standalone::saveSettings(settingsPathName, settings))
        std::cerr << "Could not save " << settingsPathName << '\n';
}

Settings StandaloneHost::currentSettings() const
{
    auto settings = Settings {};
    settings.audio = audioEngine.getConfig();
    settings.midiInputPorts = wantedInputs;
    settings.midiOutputPort = wantedOutput;
    settings.midiVirtualOutput = virtualOutput;
    return settings;
}

} // namespace MakeASound::Standalone
