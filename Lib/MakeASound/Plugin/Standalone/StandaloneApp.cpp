#include "StandaloneApp.h"
#include "EditorWindow.h"

namespace MakeASound::Standalone
{

// GenericEditor even for a plugin whose hasEditor() is false: the app has to
// show a window, and there is no host page to show instead.
OwningPointer<Editor> StandaloneApp::createEditor(StandaloneHost& host)
{
    return createWindowEditor(host.plugin());
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

    panel.onTestOutput = [&host] { host.playTestTone(); };

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

void StandaloneApp::pollHost(SettingsPanel& panel, StandaloneHost& host)
{
    if (!host.poll())
        return;

    panel.setMidiOutput(host.midiOutput());
    panel.refresh();
}

} // namespace MakeASound::Standalone
