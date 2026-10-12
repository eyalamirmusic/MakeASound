#include "StandaloneApp.h"
#include "../UI/GenericEditor.h"

namespace MakeASound::Standalone
{

OwningPointer<Editor> StandaloneApp::createEditor(StandaloneHost& host)
{
    if (auto editor = host.createEditor())
        return editor;

    return EA::makeOwned<GenericEditor>(host.plugin());
}

void StandaloneApp::connectSettings(SettingsPanel& panel, StandaloneHost& host)
{
    panel.onConfigChanged = [&panel, &host](const StreamConfig& config)
    {
        host.applyConfig(config);
        panel.setConfig(host.config());
    };

    panel.onMidiInputToggled = [&host](int id, bool open)
    { host.setMidiInputOpen(id, open); };

    panel.onMidiOutputChanged = [&panel, &host](std::optional<int> id)
    {
        host.setMidiOutput(id);
        panel.setMidiOutput(host.midiOutput());
    };

    panel.setConfig(host.config());
    panel.setMidiOutput(host.midiOutput());
}

void StandaloneApp::resetAudioAndMidi(SettingsPanel& panel, StandaloneHost& host)
{
    host.resetAudioAndMidi();

    panel.setConfig(host.config());
    panel.setMidiOutput(std::nullopt);
    panel.refresh();
}

} // namespace MakeASound::Standalone
