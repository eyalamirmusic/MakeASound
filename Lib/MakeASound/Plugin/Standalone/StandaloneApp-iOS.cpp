#include "StandaloneApp.h"

#include <eacp/Graphics/Window/Window.h>
#include <eacp/UI/UI.h>
#include <eacp/Core/Threads/Timer.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace MakeASound::Standalone
{
namespace Graphics = eacp::Graphics;
namespace Widgets = eacp::UI;

namespace
{
constexpr auto barHeight = 56.f;
constexpr auto barPadding = 8.f;
constexpr auto buttonWidth = 110.f;
constexpr auto settingsTitle = "Audio / MIDI Settings";

// The largest size the editor allows inside the area: a fixed editor its own,
// a resizable one the area fitted to its ratio and bounds. The minimum wins
// when the area is smaller, and the screen then clips the editor.
EditorSize fitEditor(const Editor& editor, const Graphics::Rect& area)
{
    auto rule = editor.constraints();

    if (!rule.resizable)
        return rule.constrain(editor.initialSize());

    auto width = static_cast<double>(area.w);
    auto height = static_cast<double>(area.h);

    if (rule.aspectRatio.width > 0 && rule.aspectRatio.height > 0)
    {
        auto scale = std::min(width / rule.aspectRatio.width,
                              height / rule.aspectRatio.height);
        width = rule.aspectRatio.width * scale;
        height = rule.aspectRatio.height * scale;
    }

    return rule.constrain(
        {static_cast<int>(std::floor(width)), static_cast<int>(std::floor(height))});
}

// Across the top of the screen: what is showing, and the button that swaps it.
class Bar final : public Widgets::Component
{
public:
    Bar()
    {
        title.setFontSize(17.f);

        addAndMakeVisible(title);
        addAndMakeVisible(button);
    }

    void paint(Widgets::Graphics& g) override
    {
        g.fillAll(Widgets::defaultTheme().panel);
    }

    void resized() override
    {
        auto area = getLocalBounds().inset(barPadding);
        button.setBounds(area.removeFromRight(buttonWidth));
        area.removeFromRight(barPadding);
        title.setBounds(area);
    }

    Widgets::Label title;
    Widgets::Button button;
};

// The window's content: the bar, then the editor centred at the size it allows,
// or the settings panel in its place. Inside the safe area, so neither runs
// under the notch or the home indicator.
class Screen final : public Graphics::View
{
public:
    Screen(Editor& editorToUse, SettingsPanel& panelToUse)
        : editor(editorToUse)
        , panel(panelToUse)
    {
        barHost.setFontPointSize(17.f);
        barHost.setRootComponent(bar);

        addSubview(barHost);
        addSubview(editor.view());
        addSubview(panel);
    }

    void show(bool settings, const std::string& pluginName)
    {
        bar.title.setText(settings ? settingsTitle : pluginName);
        bar.button.setText(settings ? "Done" : "Settings");

        panel.setVisible(settings);
        editor.view().setVisible(!settings);
    }

    Widgets::Button& button() { return bar.button; }

    void paint(Graphics::Context& context) override
    {
        context.setColor(Widgets::defaultTheme().background);
        context.fillRect(getLocalBounds());
    }

    void resized() override
    {
        auto area = getLocalBounds().inset(getSafeAreaInsets());
        barHost.setBounds(area.removeFromTop(barHeight));
        panel.setBounds(area);

        auto size = fitEditor(editor, area);
        auto width = static_cast<float>(size.width);
        auto height = static_cast<float>(size.height);

        editor.view().setBounds({area.x + std::max(0.f, (area.w - width) * 0.5f),
                                 area.y + std::max(0.f, (area.h - height) * 0.5f),
                                 width,
                                 height});
    }

    void safeAreaInsetsChanged() override { resized(); }

private:
    Editor& editor;
    SettingsPanel& panel;

    Bar bar;
    Widgets::ComponentHost barHost;
};

// The screen is the size the system gives it, so a request is refused; the
// editor draws no grip, since there is nothing to drag.
struct ScreenHost final : EditorHost
{
    bool resizeEditor(EditorSize) override { return false; }
    bool drawsResizeHandle() const override { return true; }
};

SettingsPanelOptions touchOptions(const BusLayout& layout)
{
    auto options = SettingsPanelOptions::forLayout(layout);
    options.touch = true;
    return options;
}
} // namespace

// iOS's shell: one window, one screen. The editor under a bar whose button
// shows the settings panel in its place, the panel ending in the two resets the
// desktop keeps in its menu bar. No typing keyboard; an on-screen one is later.
struct StandaloneApp::Shell
{
    explicit Shell(StandaloneHost& hostToUse);
    ~Shell();

    void showSettings(bool show);

    StandaloneHost& host;
    std::string pluginName {host.plugin().name()};
    bool settingsShown = false;

    OwningPointer<Editor> editor = createEditor(host);
    SettingsPanel panel {
        host.devices(), host.midi(), touchOptions(host.busLayout())};
    Screen screen {*editor, panel};
    OwningPointer<Graphics::Window> window;

    ScreenHost editorHost;

    // Reopens a picked MIDI port plugged back in. Last, so it stops first.
    eacp::Threads::Timer pollTimer {[this] { pollHost(panel, host); }, pollHz};
};

StandaloneApp::StandaloneApp()
{
    host.start();
    shell = EA::makeOwned<Shell>(host);
}

StandaloneApp::~StandaloneApp() = default;

StandaloneApp::Shell::Shell(StandaloneHost& hostToUse)
    : host(hostToUse)
{
    connectSettings(panel, host);
    panel.addAction("Reset Plugin to Defaults", [this] { host.resetPlugin(); });
    panel.addAction("Reset Audio / MIDI Settings",
                    [this] { resetAudioAndMidi(panel, host); });

    screen.button().onClick = [this] { showSettings(!settingsShown); };
    showSettings(false);

    auto options = Graphics::WindowOptions {};
    options.title = pluginName;

    window = EA::makeOwned<Graphics::Window>(options);
    window->setContentView(screen);
    editor->setHost(&editorHost);
    editor->onAttached();
}

StandaloneApp::Shell::~Shell()
{
    editor->onRemoved();
    editor->setHost(nullptr);
}

void StandaloneApp::Shell::showSettings(bool show)
{
    settingsShown = show;
    screen.show(show, pluginName);
}

} // namespace MakeASound::Standalone
