// The Gain plugin in an app of its own rather than the default standalone: one
// window, a toolbar of the app's above the plugin's editor, and the settings
// window the default app uses. StandaloneHost runs the plugin; everything here is
// UI the app chose.

#include <MakeASound/Plugin/Standalone/MakeASoundStandalone.h>

#include <eacp/Core/App/App.h>
#include <eacp/Graphics/Menu/Menu.h>
#include <eacp/Graphics/Window/Window.h>
#include <eacp/UI/UI.h>

#include <cmath>
#include <string>

namespace MakeASoundExamples
{
using namespace MakeASound;
using namespace MakeASound::Standalone;
namespace Graphics = eacp::Graphics;
namespace Widgets = eacp::UI;

namespace
{
constexpr auto appName = "Gain App";
constexpr auto toolbarHeight = 40;
constexpr auto padding = 8.f;
constexpr auto buttonWidth = 170.f;

OwningPointer<Plugin> createGain()
{
    return describeModule().plugins[0].create();
}

OwningPointer<Editor> createEditorFor(StandaloneHost& host)
{
    if (auto editor = host.createEditor())
        return editor;

    return EA::makeOwned<GenericEditor>(host.plugin());
}

std::string describeStream(const StreamConfig& config)
{
    if (!config.output)
        return "No output device";

    return config.output->device.name + ", " + std::to_string(config.sampleRate)
           + " Hz / " + std::to_string(config.maxBlockSize);
}

// The editor's own window rule, with the toolbar on top of it.
Graphics::WindowOptions windowOptions(const Editor& editor)
{
    auto options = editorWindowOptions(editor, appName);
    auto editorRule = options.sizeConstraint;

    options.height += toolbarHeight;
    options.minHeight += toolbarHeight;

    options.sizeConstraint = [editorRule](const Graphics::ResizeRequest& request)
    {
        auto below = request;
        below.size.y -= static_cast<float>(toolbarHeight);

        auto size = editorRule(below);
        return Graphics::Point {size.x, size.y + static_cast<float>(toolbarHeight)};
    };

    return options;
}

// The app's own control: the settings button and the stream the engine runs.
class Toolbar final : public Widgets::Component
{
public:
    Toolbar()
    {
        status.setColour(Widgets::defaultTheme().dimText);
        status.setFontSize(12.f);

        addAndMakeVisible(settings);
        addAndMakeVisible(status);
    }

    void resized() override
    {
        auto area = getLocalBounds().inset(padding);
        settings.setBounds(area.removeFromLeft(buttonWidth));
        area.removeFromLeft(2.f * padding);
        status.setBounds(area);
    }

    Widgets::Button settings {"Audio / MIDI Settings..."};
    Widgets::Label status;
};

// The toolbar above the editor's view, which the editor owns.
class AppView final : public Graphics::View
{
public:
    explicit AppView(Graphics::View& editorView)
        : editor(editorView)
    {
        bar.setRootComponent(toolbar);
        addSubview(bar);
        addSubview(editor);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        bar.setBounds(area.removeFromTop(static_cast<float>(toolbarHeight)));
        editor.setBounds(area);
    }

    // Before the host showing it, which must not outlive it.
    Toolbar toolbar;

private:
    Widgets::ComponentHost bar;
    Graphics::View& editor;
};
} // namespace

// The host is the first member, so it goes last: the windows and the editor
// first, then the stream, then the wrapper, as PluginWrapper asserts.
class GainApp
{
public:
    GainApp()
    {
        host.start();

        createSettingsWindow();
        showStream();

        view.toolbar.settings.onClick = [this]
        {
            settingsWindow->setVisible(true);
            settingsWindow->toFront();
        };

        window = EA::makeOwned<Graphics::Window>(windowOptions(*editor));
        window->setContentView(view);
        editor->setHost(&windowHost);
        editor->onAttached();

        auto bar = Graphics::MenuBar {};
        bar.add(Graphics::standardApplicationMenu(appName));
        Graphics::setApplicationMenuBar(bar, *window);
    }

    ~GainApp()
    {
        editor->onRemoved();
        editor->setHost(nullptr);
    }

    GainApp(const GainApp&) = delete;
    GainApp& operator=(const GainApp&) = delete;

private:
    // Asked by the editor for a size: the window takes it, plus the toolbar.
    struct WindowHost : EditorHost
    {
        explicit WindowHost(GainApp& appToUse)
            : app(appToUse)
        {
        }

        bool resizeEditor(EditorSize size) override
        {
            auto& window = *app.window;
            window.setSize({static_cast<float>(size.width),
                            static_cast<float>(size.height + toolbarHeight)});

            auto actual = window.getSize();
            return std::lround(actual.x) == size.width
                   && std::lround(actual.y) == size.height + toolbarHeight;
        }

        bool drawsResizeHandle() const override { return true; }

        GainApp& app;
    };

    void createSettingsWindow()
    {
        panel.onConfigChanged = [this](const StreamConfig& config)
        {
            host.applyConfig(config);
            panel.setConfig(host.config());
            showStream();
        };

        panel.onMidiInputToggled = [this](int id, bool open)
        { host.setMidiInputOpen(id, open); };

        panel.setConfig(host.config());

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
    }

    void showStream() { view.toolbar.status.setText(describeStream(host.config())); }

    // Its own settings file, apart from the default Gain standalone's.
    StandaloneHost host {createGain(), settingsPath("MakeASound", appName)};

    OwningPointer<Editor> editor = createEditorFor(host);
    AppView view {editor->view()};
    OwningPointer<Graphics::Window> window;

    SettingsPanel panel {host.devices(),
                         host.midi(),
                         SettingsPanelOptions::forLayout(host.busLayout())};
    OwningPointer<Graphics::Window> settingsWindow;

    WindowHost windowHost {*this};
};

} // namespace MakeASoundExamples

int main()
{
    return eacp::Apps::run<MakeASoundExamples::GainApp>();
}
