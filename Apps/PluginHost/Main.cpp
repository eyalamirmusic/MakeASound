// PluginHost: the example plugin host. One window lists what is installed (a
// filter box, the list, Scan and Rescan All), double-clicking a row loads it and
// plays it on the picked audio devices, Show Editor opens the plugin's own editor
// in a window of its own, and Audio / MIDI opens the standalone's settings panel
// to pick the devices, rate, block size and MIDI inputs, kept across launches.
// Everything but the windows is PluginHost.h.
//
// The scan runs in this process and loads each plugin in a copy of this app
// relaunched with --makeasound-scan (Apps/ScanProtocol), so main() answers that
// first.

#include "PluginHost.h"

#include <ScanProtocol/ScanWorker.h>

#include <MakeASound/Hosting/HostedEditor.h>
#include <MakeASound/Plugin/Standalone/SettingsPanel.h>

#include <eacp/Core/Core.h>
#include <eacp/Graphics/Graphics.h>
#include <eacp/UI/UI.h>

namespace PluginHost
{
namespace Widgets = eacp::UI;
namespace Graphics = eacp::Graphics;

// The list's rows: name, vendor and format of each plugin the host lists.
struct PluginRows final : Widgets::ListBoxModel
{
    explicit PluginRows(Host& hostToUse)
        : host(hostToUse)
    {
    }

    int getNumRows() override { return host.plugins().size(); }

    void paintRow(Widgets::Graphics& g,
                  int row,
                  const Graphics::Rect& bounds,
                  bool selected) override
    {
        const auto& plugin = host.plugins()[row];
        const auto& theme = Widgets::defaultTheme();

        if (selected)
        {
            g.setColour(theme.get(Widgets::ThemeKeys::accentDim));
            g.fillRect(bounds);
        }

        auto text = bounds.inset(8.f, 0.f);
        g.setColour(theme.get(Widgets::ThemeKeys::text));
        g.drawText(plugin.name, text.removeFromLeft(text.w * 0.5f));
        g.setColour(theme.get(Widgets::ThemeKeys::dimText));
        g.drawText(plugin.vendor, text.removeFromLeft(text.w * 0.7f));
        g.drawText(plugin.format == PluginFormat::AU ? "AU" : "VST3", text);
    }

    void rowDoubleClicked(int row) override { onLoad(row); }

    Host& host;
    std::function<void(int row)> onLoad = [](int) {};
};

// The editor's window resizes when the plugin asks.
struct EditorWindowHost final : EditorHost
{
    bool resizeEditor(EditorSize size) override
    {
        if (window == nullptr)
            return false;

        window->setSize(
            {static_cast<float>(size.width), static_cast<float>(size.height)});
        return true;
    }

    bool drawsResizeHandle() const override { return false; }

    Graphics::Window* window = nullptr;
};

class MainPanel final : public Widgets::Component
{
public:
    explicit MainPanel(Host& hostToUse)
        : host(hostToUse)
    {
        filter.setPlaceholder("Filter by name, vendor or type");
        filter.onTextChange = [this](const std::string& text)
        { host.setFilter(text); };

        scan.onClick = [this] { host.scan(); };
        rescan.onClick = [this] { host.scan(true); };
        showEditor.onClick = [this] { openEditor(); };
        showSettings.onClick = [this] { openSettings(); };

        // An open editor closes before the plugin under it is replaced. The
        // default devices depend on the plugin, so the panel shows them again.
        rows.onLoad = [this](int row)
        {
            closeEditor();
            host.load(row);
            settings.setConfig(host.config());
        };

        connectSettings();

        list.setModel(&rows);
        list.setRowHeight(24.f);

        addAndMakeVisible(filter);
        addAndMakeVisible(scan);
        addAndMakeVisible(rescan);
        addAndMakeVisible(showEditor);
        addAndMakeVisible(showSettings);
        addAndMakeVisible(list);
        addAndMakeVisible(status);

        host.onChanged = [this]
        {
            list.updateContent();
            status.setText(host.status());
        };
    }

    ~MainPanel() override { closeEditor(); }

    void resized() override
    {
        auto area = getLocalBounds().inset(12.f);
        auto top = area.removeFromTop(28.f);

        showSettings.setBounds(top.removeFromRight(110.f));
        top.removeFromRight(8.f);
        showEditor.setBounds(top.removeFromRight(110.f));
        top.removeFromRight(8.f);
        rescan.setBounds(top.removeFromRight(100.f));
        top.removeFromRight(8.f);
        scan.setBounds(top.removeFromRight(80.f));
        top.removeFromRight(8.f);
        filter.setBounds(top);

        status.setBounds(area.removeFromBottom(24.f));
        area.removeFromTop(8.f);
        list.setBounds(area);
    }

    // The loaded plugin's own editor, in a window of its own.
    void openEditor()
    {
        closeEditor();
        editor = host.createEditor();

        if (editor == nullptr)
            return;

        auto size = editor->initialSize();
        auto options = Graphics::WindowOptions();
        options.title = host.plugin()->name();
        options.width = size.width;
        options.height = size.height;
        options.isPrimary = false;

        editorWindow = EA::makeOwned<Graphics::Window>(options);
        editorHost.window = editorWindow.get();
        editor->setHost(&editorHost);
        editorWindow->setContentView(editor->view());
        editor->onAttached();
    }

    // The settings panel in a window of its own, hidden rather than closed, as
    // the standalone shows it.
    void openSettings()
    {
        if (settingsWindow == nullptr)
        {
            auto options = Graphics::WindowOptions();
            options.title = "Audio / MIDI Settings";
            options.width = settings.preferredWidth();
            options.height = settings.preferredHeight();
            options.isPrimary = false;
            options.hidesOnClose = true;

            settingsWindow = EA::makeOwned<Graphics::Window>(options);
            settingsWindow->setContentView(settings);
        }

        settings.setConfig(host.config());
        settingsWindow->setVisible(true);
        settingsWindow->toFront();
    }

    // The panel edits a copy; the host applies it and the panel shows what the
    // engine settled on, as the standalone connects its own.
    void connectSettings()
    {
        settings.onConfigChanged = [this](const StreamConfig& config)
        {
            host.applyConfig(config);
            settings.setConfig(host.config());
        };

        settings.onMidiInputToggled = [this](int id, bool open)
        { host.setMidiInputOpen(id, open); };

        settings.addAction("Reset Audio / MIDI Settings",
                           [this]
                           {
                               host.resetAudioAndMidi();
                               settings.setConfig(host.config());
                           });

        settings.setConfig(host.config());
    }

    // Before the plugin is replaced: the view goes while the plugin lives.
    void closeEditor()
    {
        if (editor != nullptr)
        {
            editor->onRemoved();
            editor->setHost(nullptr);
        }

        editorWindow = nullptr;
        editorHost.window = nullptr;
        editor = nullptr;
    }

private:
    Host& host;
    PluginRows rows {host};

    Widgets::TextEditor filter;
    Widgets::Button scan {"Scan"};
    Widgets::Button rescan {"Rescan All"};
    Widgets::Button showEditor {"Show Editor"};
    Widgets::Button showSettings {"Audio / MIDI"};
    Widgets::ListBox list;
    Widgets::Label status;

    OwningPointer<Editor> editor;
    OwningPointer<Graphics::Window> editorWindow;
    EditorWindowHost editorHost;

    // Every row: what the host may load changes, and an input is the user's
    // pick. The window goes before the panel it shows.
    Standalone::SettingsPanel settings {
        host.deviceManager(),
        host.midiManager(),
        {.showInput = true, .showMidiInputs = true, .showMidiOutput = false}};
    OwningPointer<Graphics::Window> settingsWindow;
};

struct App
{
    App()
    {
        root.setRootComponent(panel);
        window.setContentView(root);

        // The picked inputs (every one on a first launch) stay open across
        // loads; the panel ticks what is open each time it is shown.
        host.openMidiInputs();

        // The database this app already has; a first launch scans.
        if (host.plugins().empty())
            host.scan();
    }

    Host host {editorOptions()};
    MainPanel panel {host};
    Widgets::ComponentHost root;
    Graphics::Window window {windowOptions()};

    // The plugin's own GUI follows what its processor reports, at 20 Hz.
    eacp::Threads::Timer idle {[this] { host.idle(); }, 20};

    static HostOptions editorOptions()
    {
        auto options = HostOptions();
        options.scan.worker = MakeASound::ScanProtocol::defaultWorkerCommand();
        options.editors = Hosting::hostedEditors();
        options.settingsFile = defaultSettingsPath();
        return options;
    }

    static Graphics::WindowOptions windowOptions()
    {
        auto options = Graphics::WindowOptions();
        options.title = "MakeASound PluginHost";
        options.width = 720;
        options.height = 520;
        options.minWidth = 480;
        options.minHeight = 320;
        return options;
    }
};

} // namespace PluginHost

namespace
{
// This app relaunched as a scan worker: load the one plugin, answer, leave.
int runScanWorker()
{
    auto formats = MakeASound::Hosting::FormatManager();
    formats.addDefaultFormats();

    return MakeASound::ScanProtocol::runScanWorkerIfRequested(formats).value_or(0);
}
} // namespace

// The command line is stored once, here; the worker check, the worker and the
// app all read it from eacp.
int main(int argc, char** argv)
{
    eacp::Apps::setCommandLineArgs(argc, argv);

    if (MakeASound::ScanProtocol::requestedScanTarget())
        return eacp::Apps::runConsole(runScanWorker);

    return eacp::Apps::run<PluginHost::App>();
}
