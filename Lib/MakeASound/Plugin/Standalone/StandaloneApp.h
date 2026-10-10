#pragma once

#include "MidiSender.h"
#include "Settings.h"
#include "SettingsPanel.h"
#include "StandaloneProcessor.h"
#include "TypingKeyboard.h"
#include "../Description.h"
#include "../Editor.h"
#include "../PluginWrapper.h"
#include "../../Devices/DeviceManager.h"
#include "../../Devices/Engine.h"
#include "../../MIDI/MidiManager.h"

#include <eacp/Graphics/Window/Window.h>

#include <optional>
#include <string>

namespace MakeASound::Standalone
{

// The standalone format: the module's first plugin in a window, run by Engine on
// the devices the settings window picks, its session saved on every settings
// change and on quit. Members are torn down in reverse: the windows and the
// editor first, then the stream, then the wrapper, all on the main thread.
class StandaloneApp
{
public:
    StandaloneApp();
    ~StandaloneApp();

    StandaloneApp(const StandaloneApp&) = delete;
    StandaloneApp& operator=(const StandaloneApp&) = delete;

private:
    struct KeyRouter : eacp::Graphics::WindowInputListener
    {
        explicit KeyRouter(StandaloneApp& appToUse)
            : app(appToUse)
        {
        }

        void windowKeyEvent(const eacp::Graphics::KeyEvent& event) override;
        void windowActivationChanged(bool isKey) override;

        StandaloneApp& app;
    };

    static OwningPointer<Plugin> createFirstPlugin(const ModuleDescription& module);

    bool hasMidiIn() const { return !wrapper.busLayout().midiInputs.empty(); }
    bool hasMidiOut() const { return !wrapper.busLayout().midiOutputs.empty(); }

    void restoreMidi(const Settings& saved);
    void startEngine(const StreamConfig& config);
    void createEditorWindow();
    void createSettingsWindow();
    void installMenuBar();

    void setMidiOutput(std::optional<int> portId);
    void toggleTyping();
    void showSettings();
    void resetAudioAndMidi();
    void saveSettingsNow();
    void saveSettingsUnchecked();

    ModuleDescription module = describeModule();
    DeviceManager devices;
    MidiManager midi;
    MidiSender sender {midi};
    PluginWrapper wrapper {createFirstPlugin(module), PluginFormat::Standalone};
    StandaloneProcessor processor {wrapper, sender};
    Engine engine {devices, midi};

    std::string settingsFile;
    std::string defaultState;
    StreamConfig fallbackConfig;
    // MidiManager cannot say which output is open.
    std::optional<int> midiOutput;

    TypingKeyboard typing {[this](auto& event)
                           { return processor.injectMidi(event); }};
    bool typingEnabled = false;

    OwningPointer<Editor> editor;
    OwningPointer<eacp::Graphics::Window> editorWindow;

    SettingsPanel panel {
        devices, midi, SettingsPanelOptions::forLayout(wrapper.busLayout())};
    OwningPointer<eacp::Graphics::Window> settingsWindow;

    KeyRouter keys {*this};
};

} // namespace MakeASound::Standalone
