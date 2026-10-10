#include "StandaloneApp.h"
#include "../UI/GenericEditor.h"

#include <eacp/Graphics/Menu/Menu.h>

#include <cassert>
#include <iostream>

namespace MakeASound::Standalone
{
namespace Graphics = eacp::Graphics;

namespace
{
void reportIfFailed(Error error)
{
    if (error != Error::NoError)
        std::cerr << getErrorMessage(error) << '\n';
}

void removeResizable(Graphics::WindowOptions& options)
{
    options.flags.eraseIf([](Graphics::WindowFlags flag)
                          { return flag == Graphics::WindowFlags::Resizable; });
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
    StandaloneApp::createFirstPlugin(const ModuleDescription& module)
{
    assert(!module.plugins.empty() && "describeModule() names no plugin");
    return module.plugins[0].create();
}

void StandaloneApp::KeyRouter::windowKeyEvent(const Graphics::KeyEvent& event)
{
    if (!app.typingEnabled)
        return;

    auto& mods = event.modifiers;

    if (event.type == Graphics::KeyEventType::Down)
        app.typing.keyDown(
            event.keyCode, event.isRepeat, mods.command || mods.control || mods.alt);
    else
        app.typing.keyUp(event.keyCode);
}

void StandaloneApp::KeyRouter::windowActivationChanged(bool isKey)
{
    if (!isKey)
        app.typing.allNotesOff();
}

StandaloneApp::StandaloneApp()
{
    settingsFile = settingsPath(module.vendor, wrapper.plugin().name());
    // A preset, so resetting leaves session-only parameters where they are.
    defaultState = wrapper.saveState(StateContext::Preset);

    auto saved = loadSettings(settingsFile);

    if (saved && !saved->pluginState.empty())
        wrapper.loadState(saved->pluginState, StateContext::Session);

    // Output-only even for an effect: a default microphone feeding the default
    // speakers is a feedback loop, so an input is only ever what the user picked.
    fallbackConfig = devices.getDefaultOutputConfig();

    if (saved)
        restoreMidi(*saved);

    startEngine(
        saved ? resolveConfig(saved->audio, devices.getDevices(), fallbackConfig)
              : fallbackConfig);

    typingEnabled = hasMidiIn();

    createEditorWindow();
    createSettingsWindow();
    installMenuBar();
}

StandaloneApp::~StandaloneApp()
{
    saveSettingsNow();

    editorWindow->events.input.removeListener(keys);
    settingsWindow->events.input.removeListener(keys);

    editor->onRemoved();
}

void StandaloneApp::restoreMidi(const Settings& saved)
{
    if (hasMidiIn())
        for (auto id: resolvePortIds(saved.midiInputPorts, midi.getInputPorts()))
            reportIfFailed(midi.openInput(id));

    if (!hasMidiOut())
        return;

    if (auto id = findPort(saved.midiOutputPort, midi.getOutputPorts()))
        setMidiOutput(id);
}

void StandaloneApp::startEngine(const StreamConfig& config)
{ reportIfFailed(engine.start(config, processor)); }

void StandaloneApp::createEditorWindow()
{
    editor = wrapper.plugin().createEditor();

    if (!editor)
        editor = EA::makeOwned<GenericEditor>(wrapper.plugin());

    auto size = editor->initialSize();
    auto options = Graphics::WindowOptions {};
    options.title = std::string(wrapper.plugin().name());
    options.width = size.width;
    options.height = size.height;

    if (!editor->isResizable())
        removeResizable(options);

    editorWindow = EA::makeOwned<Graphics::Window>(options);
    editorWindow->setContentView(editor->view());
    editorWindow->events.input.addListener(keys);
    editor->onAttached();
}

void StandaloneApp::createSettingsWindow()
{
    panel.onConfigChanged = [this](const StreamConfig& config)
    {
        startEngine(config);
        panel.setConfig(engine.getConfig());
        saveSettingsNow();
    };

    panel.onMidiInputToggled = [this](int id, bool open)
    {
        if (open)
            reportIfFailed(midi.openInput(id));
        else
            midi.closeInput(id);

        saveSettingsNow();
    };

    panel.onMidiOutputChanged = [this](std::optional<int> id)
    {
        setMidiOutput(id);
        panel.setMidiOutput(midiOutput);
        saveSettingsNow();
    };

    panel.setConfig(engine.getConfig());
    panel.setMidiOutput(midiOutput);

    auto options = Graphics::WindowOptions {};
    options.title = "Audio / MIDI Settings";
    options.width = panel.preferredWidth();
    options.height = panel.preferredHeight();
    options.isPrimary = false;
    options.hidesOnClose = true;
    removeResizable(options);

    settingsWindow = EA::makeOwned<Graphics::Window>(options);
    settingsWindow->setContentView(panel);
    settingsWindow->setVisible(false);
    settingsWindow->events.input.addListener(keys);
}

void StandaloneApp::installMenuBar()
{
    using Graphics::commandKey;
    using Graphics::MenuItem;

    auto name = std::string(wrapper.plugin().name());
    auto appMenu = Graphics::standardApplicationMenu(name);

    // Below "About" and its separator.
    auto at = appMenu.items.size() >= 2 ? 2 : appMenu.items.size();

    appMenu.items.insert(
        at++,
        MenuItem::withAction(
            "Audio / MIDI Settings…", [this] { showSettings(); }, commandKey(",")));

    if (hasMidiIn())
        appMenu.items.insert(at++,
                             MenuItem::withCheckableAction(
                                 "Computer MIDI Keyboard",
                                 [this] { toggleTyping(); },
                                 [this] { return typingEnabled; },
                                 commandKey("k")));

    appMenu.items.insert(at++, MenuItem::separator());

    appMenu.items.insert(at++,
                         MenuItem::withAction("Reset Plugin to Defaults",
                                              [this]
                                              {
                                                  wrapper.loadState(
                                                      defaultState,
                                                      StateContext::Preset);
                                                  saveSettingsNow();
                                              }));

    appMenu.items.insert(at++,
                         MenuItem::withAction("Reset Audio / MIDI Settings",
                                              [this] { resetAudioAndMidi(); }));

    appMenu.items.insert(at++, MenuItem::separator());

    auto bar = Graphics::MenuBar {};
    bar.add(std::move(appMenu));
    Graphics::setApplicationMenuBar(bar, *editorWindow);
}

void StandaloneApp::setMidiOutput(std::optional<int> portId)
{
    sender.stop();
    midi.closeOutput();
    midiOutput.reset();

    if (!portId)
        return;

    auto error = midi.openOutput(*portId);
    reportIfFailed(error);

    if (error != Error::NoError)
        return;

    midiOutput = portId;
    sender.start();
}

void StandaloneApp::toggleTyping()
{
    typingEnabled = !typingEnabled;

    if (!typingEnabled)
        typing.allNotesOff();
}

void StandaloneApp::showSettings()
{
    settingsWindow->setVisible(true);
    settingsWindow->toFront();
}

void StandaloneApp::resetAudioAndMidi()
{
    for (auto id: midi.getOpenInputPorts())
        midi.closeInput(id);

    setMidiOutput(std::nullopt);
    startEngine(fallbackConfig);

    panel.setConfig(engine.getConfig());
    panel.setMidiOutput(std::nullopt);
    panel.refresh();
    saveSettingsNow();
}

void StandaloneApp::saveSettingsNow()
{
    // Reached from the destructor and from event-loop callbacks, where a
    // throwing saveState has nowhere to go.
    try
    {
        saveSettingsUnchecked();
    }
    catch (const std::exception& e)
    {
        std::cerr << "Could not save " << settingsFile << ": " << e.what() << '\n';
    }
}

void StandaloneApp::saveSettingsUnchecked()
{
    auto settings = Settings {};
    settings.audio = engine.getConfig();

    auto inputs = midi.getInputPorts();

    for (auto id: midi.getOpenInputPorts())
        if (auto name = portName(id, inputs); !name.empty())
            settings.midiInputPorts.add(name);

    if (midiOutput)
        settings.midiOutputPort = portName(*midiOutput, midi.getOutputPorts());

    settings.pluginState = wrapper.saveState(StateContext::Session);

    if (!saveSettings(settingsFile, settings))
        std::cerr << "Could not save " << settingsFile << '\n';
}

} // namespace MakeASound::Standalone
