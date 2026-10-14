// PluginHost: the example plugin host. One window lists what is installed (a
// filter box, the list, Scan and Rescan All), double-clicking a row loads it and
// plays it on the default audio device, and Show Editor opens the plugin's own
// editor in a window of its own. Everything but the windows is PluginHost.h.
//
// The scan runs in this process and loads each plugin in a copy of this app
// relaunched with --makeasound-scan, so main() answers that first.

#include "PluginHost.h"

#include <MakeASound/Hosting/HostedEditor.h>

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
            g.setColour(theme.accentDim);
            g.fillRect(bounds);
        }

        auto text = bounds.inset(8.f, 0.f);
        g.setColour(theme.text);
        g.drawText(plugin.name, text.removeFromLeft(text.w * 0.5f));
        g.setColour(theme.dimText);
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

        // An open editor closes before the plugin under it is replaced.
        rows.onLoad = [this](int row)
        {
            closeEditor();
            host.load(row);
        };

        list.setModel(&rows);
        list.setRowHeight(24.f);

        addAndMakeVisible(filter);
        addAndMakeVisible(scan);
        addAndMakeVisible(rescan);
        addAndMakeVisible(showEditor);
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
    Widgets::ListBox list;
    Widgets::Label status;

    OwningPointer<Editor> editor;
    OwningPointer<Graphics::Window> editorWindow;
    EditorWindowHost editorHost;
};

struct App
{
    App()
    {
        root.setRootComponent(panel);
        window.setContentView(root);

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
        options.editors = Hosting::hostedEditors();
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

int main(int argc, char** argv)
{
    eacp::Apps::setCommandLineArgs(argc, argv);

    // This app relaunched as a scan worker: load the one plugin, answer, leave.
    if (MakeASound::Hosting::requestedScanTarget(argc, argv))
        return eacp::Apps::run(
            [argc, argv]
            {
                eacp::Apps::setDockIconVisible(false);

                auto formats = MakeASound::Hosting::FormatManager();
                formats.addDefaultFormats();

                auto code = MakeASound::Hosting::runScanWorkerIfRequested(
                    argc, argv, formats);
                eacp::Apps::quit(code.value_or(0));
            });

    return eacp::Apps::run<PluginHost::App>();
}
