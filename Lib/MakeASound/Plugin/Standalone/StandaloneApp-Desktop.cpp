#include "StandaloneApp.h"
#include "AppCommands.h"
#include "EditorWindow.h"
#include "TypingKeyboard.h"

#include <eacp/Graphics/Window/Window.h>
#include <eacp/Core/Threads/Timer.h>

#include <cmath>

namespace MakeASound::Standalone
{
namespace Graphics = eacp::Graphics;

// The desktop's shell: the editor in a window, the settings in a window of their
// own, the menu bar and the typing keyboard.
struct StandaloneApp::Shell
{
    explicit Shell(StandaloneHost& hostToUse);
    ~Shell();

    struct KeyRouter : Graphics::WindowInputListener
    {
        explicit KeyRouter(Shell& shellToUse)
            : shell(shellToUse)
        {
        }

        void windowKeyEvent(const Graphics::KeyEvent& event) override;
        void windowActivationChanged(bool isKey) override;

        Shell& shell;
    };

    struct EditorWindowHost : EditorHost
    {
        explicit EditorWindowHost(Shell& shellToUse)
            : shell(shellToUse)
        {
        }

        bool resizeEditor(EditorSize size) override;
        bool drawsResizeHandle() const override { return true; }

        // Played on the typing keyboard, and claimed whether it played or not:
        // sent on, it would reach this app's own window, which has no use for it.
        bool unhandledKey(const Graphics::KeyEvent& key) override;

        Shell& shell;
    };

    // A key event for the typing keyboard, from a window or the editor.
    void typeKey(const Graphics::KeyEvent& event);

    void createEditorWindow();
    void createSettingsWindow();
    void installMenuBar();

    void toggleTyping();
    void showSettings() const;

    StandaloneHost& host;

    TypingKeyboard typing {[this](auto& event) { return host.injectMidi(event); }};
    bool typingEnabled = false;

    OwningPointer<Editor> editor;
    OwningPointer<Graphics::Window> editorWindow;

    SettingsPanel panel {host.devices(),
                         host.midi(),
                         SettingsPanelOptions::forLayout(host.busLayout())};
    OwningPointer<Graphics::Window> settingsWindow;

    KeyRouter keys {*this};
    EditorWindowHost editorHost {*this};

    // Reopens a picked MIDI port plugged back in. Last, so it stops first.
    eacp::Threads::Timer pollTimer {[this] { pollHost(panel, host); }, pollHz};
};

void StandaloneApp::Shell::typeKey(const Graphics::KeyEvent& event)
{
    if (typingEnabled)
        typing.key(event);
}

void StandaloneApp::Shell::KeyRouter::windowKeyEvent(const Graphics::KeyEvent& event)
{
    shell.typeKey(event);
}

void StandaloneApp::Shell::KeyRouter::windowActivationChanged(bool isKey)
{
    if (!isKey)
        shell.typing.allNotesOff();
}

bool StandaloneApp::Shell::EditorWindowHost::resizeEditor(EditorSize size)
{
    auto& window = *shell.editorWindow;
    window.setSize(
        {static_cast<float>(size.width), static_cast<float>(size.height)});

    auto actual = window.getSize();
    return std::lround(actual.x) == size.width
           && std::lround(actual.y) == size.height;
}

bool StandaloneApp::Shell::EditorWindowHost::unhandledKey(
    const Graphics::KeyEvent& key)
{
    shell.typeKey(key);
    return true;
}

StandaloneApp::StandaloneApp()
{
    host.start();
    shell = EA::makeOwned<Shell>(host);
}

StandaloneApp::~StandaloneApp() = default;

StandaloneApp::Shell::Shell(StandaloneHost& hostToUse)
    : host(hostToUse)
{
    typingEnabled = host.hasMidiInput();

    createEditorWindow();
    createSettingsWindow();
    installMenuBar();
}

StandaloneApp::Shell::~Shell()
{
    editorWindow->events.input.removeListener(keys);
    settingsWindow->events.input.removeListener(keys);

    editor->onRemoved();
    editor->setHost(nullptr);
}

void StandaloneApp::Shell::installMenuBar()
{
    auto actions = AppCommands {};
    actions.appName = std::string(host.plugin().name());
    actions.showSettings = [this] { showSettings(); };
    actions.resetPlugin = [this] { host.resetPlugin(); };
    actions.resetAudioAndMidi = [this] { resetAudioAndMidi(panel, host); };

    if (host.hasMidiInput())
    {
        actions.toggleTyping = [this] { toggleTyping(); };
        actions.isTyping = [this] { return typingEnabled; };
    }

    installAppMenu(actions, *editorWindow);
}

void StandaloneApp::Shell::createEditorWindow()
{
    editor = createEditor(host);

    auto options = editorWindowOptions(*editor, std::string(host.plugin().name()));

    editorWindow = EA::makeOwned<Graphics::Window>(options);
    editorWindow->setContentView(editor->view());
    editorWindow->events.input.addListener(keys);
    editor->setHost(&editorHost);
    editor->onAttached();
}

void StandaloneApp::Shell::createSettingsWindow()
{
    connectSettings(panel, host);

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

void StandaloneApp::Shell::toggleTyping()
{
    typingEnabled = !typingEnabled;

    if (!typingEnabled)
        typing.allNotesOff();
}

void StandaloneApp::Shell::showSettings() const
{
    settingsWindow->setVisible(true);
    settingsWindow->toFront();
}

} // namespace MakeASound::Standalone
