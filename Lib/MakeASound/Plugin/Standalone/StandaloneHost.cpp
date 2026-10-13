#include "StandaloneHost.h"
#include "../Compat/JuceState.h"
#include "../Core/Identity.h"

#include <eacp/Core/Utils/Files.h>

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
} // namespace

OwningPointer<Plugin>
    StandaloneHost::createFirstPlugin(const ModuleDescription& module)
{
    assert(!module.plugins.empty() && "describeModule() names no plugin");
    return module.plugins[0].create();
}

StandaloneHost::StandaloneHost()
    : StandaloneHost(describeModule())
{
}

StandaloneHost::StandaloneHost(const ModuleDescription& module)
    : pluginWrapper(createFirstPlugin(module),
                    PluginFormat::Standalone,
                    parameterIdentityFor(module.plugins[0]))
    , settingsPathName(settingsPath(module.vendor, pluginWrapper.plugin().name()))
    , juceSettingsName(juceSettingsPath(pluginWrapper.plugin().name()))
{
    restoreSession();
}

StandaloneHost::StandaloneHost(OwningPointer<Plugin> plugin,
                               std::string settingsFile,
                               std::string juceSettingsFile,
                               ParameterIdentity identity)
    : pluginWrapper(std::move(plugin), PluginFormat::Standalone, identity)
    , settingsPathName(std::move(settingsFile))
    , juceSettingsName(std::move(juceSettingsFile))
{
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

    startEngine(
        saved ? resolveConfig(saved->audio, deviceManager.getDevices(), fallback)
              : fallback);
    saved.reset();
}

void StandaloneHost::restoreMidi(const Settings& settings)
{
    if (hasMidiInput())
        for (auto id:
             resolvePortIds(settings.midiInputPorts, midiManager.getInputPorts()))
            reportIfFailed(midiManager.openInput(id));

    if (!hasMidiOutput())
        return;

    if (auto id = findPort(settings.midiOutputPort, midiManager.getOutputPorts()))
        openMidiOutput(id);
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

    if (open)
        error = reportIfFailed(midiManager.openInput(portId));
    else
        midiManager.closeInput(portId);

    saveSettings();
    return error;
}

Error StandaloneHost::setMidiOutput(std::optional<int> portId)
{
    auto error = openMidiOutput(portId);
    saveSettings();
    return error;
}

Error StandaloneHost::openMidiOutput(std::optional<int> portId)
{
    sender.stop();
    midiManager.closeOutput();
    openOutput.reset();

    if (!portId)
        return Error::NoError;

    auto error = reportIfFailed(midiManager.openOutput(*portId));

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
    auto settings = saved ? *saved : currentSettings();
    settings.pluginState = pluginWrapper.saveState(StateContext::Session);

    if (!Standalone::saveSettings(settingsPathName, settings))
        std::cerr << "Could not save " << settingsPathName << '\n';
}

Settings StandaloneHost::currentSettings() const
{
    auto settings = Settings {};
    settings.audio = audioEngine.getConfig();

    auto inputs = midiManager.getInputPorts();

    for (auto id: midiManager.getOpenInputPorts())
        if (auto name = portName(id, inputs); !name.empty())
            settings.midiInputPorts.add(name);

    if (openOutput)
        settings.midiOutputPort =
            portName(*openOutput, midiManager.getOutputPorts());

    return settings;
}

} // namespace MakeASound::Standalone
