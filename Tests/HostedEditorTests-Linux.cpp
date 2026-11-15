// Hosted editors on Linux: the VST3 fixtures' eacp editors parented into an eacp
// NativeChildSurface in an X11 window of the test's, resized through it, asking
// to be resized, and removed; and what happens with no editor factory or no X11
// window. The window cases need an X server and eacp's X11 backend
// (EACP_WINDOW_SYSTEM=x11, as eacp's Scripts/with-xvfb sets) and skip without
// one, or fail when EACP_REQUIRE_DISPLAY=1. What they read back comes from the
// server, over a connection of the test's own.

#include "HostFixtures/InstrumentEditor.h"

#include <MakeASound/Plugin/VST3/VST3Common.h>
#include <MakeASound/Hosting/MakeASoundHosting.h>
#include <MakeASound/Hosting/HostedEditor.h>
#include <MakeASound/Hosting/VST3/HostedVST3Editor.h>

#include <eacp/Core/App/AppEnvironment.h>
#include <eacp/Core/Threads/EventLoop.h>
#include <eacp/Core/Utils/Environment.h>
#include <eacp/Graphics/Window/NativeChildSurface.h>
#include <eacp/Graphics/Window/Window.h>

#include <NanoTest/NanoTest.h>

#include <xcb/xcb.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace HostedEditorTests
{
using namespace nano;
using namespace MakeASound;
using namespace MakeASound::Hosting;

namespace
{
struct DefaultFormats : FormatManager
{
    DefaultFormats() { addDefaultFormats(); }
};

std::string fixture(const char* name)
{
    return (std::filesystem::path(MAKEASOUND_HOST_FIXTURE_VST3_DIR)
            / (std::string(name) + ".vst3"))
        .string();
}

bool x11Reachable()
{
    if (eacp::Apps::getAppEnvironment().headless)
        return false;

    if (eacp::getEnvValue("EACP_WINDOW_SYSTEM") != "x11")
        return false;

    return !eacp::getEnvValue("DISPLAY").empty();
}

// False, and the case skips, without an X server; a failure instead when the
// run says it has one (EACP_REQUIRE_DISPLAY=1).
bool hasDisplay()
{
    if (x11Reachable())
        return true;

    check(eacp::getEnvValue("EACP_REQUIRE_DISPLAY") != "1");
    return false;
}

template <typename Predicate>
bool pumpUntil(Predicate ready)
{
    return eacp::Threads::runEventLoopUntil(ready, eacp::Time::MS {3000});
}

// The test's own view of the server.
struct Connection
{
    Connection()
        : xcb(xcb_connect(nullptr, nullptr))
    {
    }

    ~Connection() { xcb_disconnect(xcb); }

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    bool isValid() const { return xcb_connection_has_error(xcb) == 0; }

    xcb_query_tree_reply_t* treeOf(xcb_window_t window) const
    {
        return xcb_query_tree_reply(xcb, xcb_query_tree(xcb, window), nullptr);
    }

    int childCount(xcb_window_t window) const
    {
        auto* reply = treeOf(window);

        if (reply == nullptr)
            return -1;

        const auto count = xcb_query_tree_children_length(reply);
        std::free(reply);
        return count;
    }

    xcb_window_t firstChild(xcb_window_t window) const
    {
        auto* reply = treeOf(window);

        if (reply == nullptr)
            return XCB_NONE;

        const auto child = xcb_query_tree_children_length(reply) > 0
                               ? xcb_query_tree_children(reply)[0]
                               : XCB_NONE;
        std::free(reply);
        return child;
    }

    EditorSize sizeOf(xcb_window_t window) const
    {
        auto* reply =
            xcb_get_geometry_reply(xcb, xcb_get_geometry(xcb, window), nullptr);

        if (reply == nullptr)
            return {};

        const auto size = EditorSize {reply->width, reply->height};
        std::free(reply);
        return size;
    }

    xcb_window_t root() const
    {
        return xcb_setup_roots_iterator(xcb_get_setup(xcb)).data->root;
    }

    // The root's RESOURCE_MANAGER, where Xft.dpi, and so an X11 window's scale,
    // comes from.
    std::string resources() const
    {
        auto cookie = xcb_get_property(
            xcb, 0, root(), XCB_ATOM_RESOURCE_MANAGER, XCB_ATOM_STRING, 0, 65536);
        auto* reply = xcb_get_property_reply(xcb, cookie, nullptr);

        if (reply == nullptr)
            return {};

        auto text = std::string(
            static_cast<const char*>(xcb_get_property_value(reply)),
            static_cast<std::size_t>(xcb_get_property_value_length(reply)));
        std::free(reply);
        return text;
    }

    void setResources(const std::string& text) const
    {
        xcb_change_property(xcb,
                            XCB_PROP_MODE_REPLACE,
                            root(),
                            XCB_ATOM_RESOURCE_MANAGER,
                            XCB_ATOM_STRING,
                            8,
                            static_cast<std::uint32_t>(text.size()),
                            text.c_str());
        xcb_flush(xcb);
    }

    xcb_connection_t* xcb;
};

xcb_window_t surfaceOf(Editor& editor)
{
    auto& surface = static_cast<eacp::Graphics::NativeChildSurface&>(editor.view());
    return static_cast<xcb_window_t>(
        reinterpret_cast<std::uintptr_t>(surface.getNativeParentHandle()));
}

bool sameSize(EditorSize a, EditorSize b)
{
    return a.width == b.width && a.height == b.height;
}

struct RecordingHost : EditorHost
{
    bool resizeEditor(EditorSize size) override
    {
        requests.add(size);
        return true;
    }

    bool drawsResizeHandle() const override { return false; }

    Vector<EditorSize> requests;
};

// An editor's view as the content of an X11 toplevel of eacp's.
struct Embedding
{
    explicit Embedding(Editor& editorToUse)
        : editor(editorToUse)
    {
        auto size = editor.initialSize();
        auto options = eacp::Graphics::WindowOptions();
        options.width = size.width;
        options.height = size.height;
        options.title = "MakeASound hosted editor tests";
        options.isPrimary = false;

        window = EA::makeOwned<eacp::Graphics::Window>(options);
        window->setContentView(editor.view());
        visible = pumpUntil([this] { return window->isVisible(); });

        editor.setHost(&host);
        editor.onAttached();
    }

    ~Embedding()
    {
        editor.onRemoved();
        editor.setHost(nullptr);
    }

    Editor& editor;
    RecordingHost host;
    OwningPointer<eacp::Graphics::Window> window;
    bool visible = false;
};

OwningPointer<Editor>
    editorFor(const FormatManager& formats, const char* name, LoadResult& loaded)
{
    auto scan = formats.scanTarget(fixture(name)).scan;

    if (scan.classes.empty())
        return {};

    loaded = formats.load(scan.classes[0], {.editors = hostedEditors()});
    return loaded.plugin != nullptr ? loaded.plugin->createEditor() : nullptr;
}

auto tNoFactory = test("HostedEditor/aHostWithoutEditorsShowsNone") = []
{
    const auto formats = DefaultFormats();
    auto scan = formats.scanTarget(fixture("HostFixtureEffect")).scan;
    check(!scan.classes.empty());

    if (scan.classes.empty())
        return;

    auto description = scan.classes[0];
    auto plain = formats.load(description);
    check(plain.plugin != nullptr && plain.plugin->createEditor() == nullptr);

    auto shown = formats.load(description, {.editors = hostedEditors()});
    check(shown.plugin != nullptr && shown.plugin->createEditor() != nullptr);
};

// No X11 window (headless here; a Wayland one alike): nothing to parent into,
// so the view stays empty and nothing is attached or crashes.
auto tNoWindow = test("HostedEditor/withoutAnX11WindowTheViewStaysEmpty") = []
{
    const auto formats = DefaultFormats();
    auto loaded = LoadResult();
    auto editor = editorFor(formats, "HostFixtureEffect", loaded);
    check(dynamic_cast<HostedVST3Editor*>(editor.get()) != nullptr);

    if (editor == nullptr)
        return;

    editor->onAttached();
    check(surfaceOf(*editor) == XCB_NONE);
    editor->onRemoved();
};

auto tVST3 = test("HostedEditor/aVST3ViewAttachesResizesAndDetaches") = []
{
    if (!hasDisplay())
        return;

    const auto connection = Connection();
    check(connection.isValid());

    const auto formats = DefaultFormats();
    auto loaded = LoadResult();
    auto editor = editorFor(formats, "HostFixtureEffect", loaded);
    auto* hosted = dynamic_cast<HostedVST3Editor*>(editor.get());
    check(hosted != nullptr);

    if (hosted == nullptr || !connection.isValid())
        return;

    auto size = hosted->initialSize();
    check(size.width > 0 && size.height > 0);

    auto surface = xcb_window_t {XCB_NONE};

    {
        auto embedding = Embedding(*hosted);
        check(embedding.visible);

        surface = surfaceOf(*hosted);
        check(surface != XCB_NONE);
        check(pumpUntil([&] { return connection.childCount(surface) == 1; }));

        const auto pluginWindow = connection.firstChild(surface);

        if (hosted->constraints().resizable && pluginWindow != XCB_NONE)
        {
            auto target = hosted->constrainSize({size.width + 40, size.height + 30});
            hosted->view().setBounds({0.f,
                                      0.f,
                                      static_cast<float>(target.width),
                                      static_cast<float>(target.height)});

            check(pumpUntil(
                [&] { return sameSize(connection.sizeOf(pluginWindow), target); }));
        }
    }

    // Removed with the embedding: the plugin took its window out.
    check(pumpUntil([&] { return connection.childCount(surface) <= 0; }));
};

// Xft.dpi 192 on this suite's own Xvfb (Scripts/with-xvfb sets
// EACP_XVFB_OUTPUT; a desktop's resource database is never touched), put back
// when the case ends.
struct ScopedDpi
{
    ScopedDpi(const Connection& connectionToUse, int dpi)
        : connection(connectionToUse)
        , original(connection.resources())
    {
        connection.setResources("Xft.dpi:\t" + std::to_string(dpi) + "\n");
    }

    ~ScopedDpi() { connection.setResources(original); }

    ScopedDpi(const ScopedDpi&) = delete;
    ScopedDpi& operator=(const ScopedDpi&) = delete;

    const Connection& connection;
    std::string original;
};

EditorSize times(EditorSize size, int factor)
{
    return {size.width * factor, size.height * factor};
}

// At scale 2 the plugin's window is twice the view's points in pixels, and the
// plugin reports its size in pixels at that scale, so the host's points stay put:
// what it does only once told the content scale. Back at 1 when the DPI is.
auto tScale = test("HostedEditor/aVST3ViewFollowsTheWindowsScale") = []
{
    if (!hasDisplay() || eacp::getEnvValue("EACP_XVFB_OUTPUT").empty())
        return;

    const auto connection = Connection();
    check(connection.isValid());

    const auto formats = DefaultFormats();
    auto loaded = LoadResult();
    auto editor = editorFor(formats, "HostFixtureEffect", loaded);
    auto* hosted = dynamic_cast<HostedVST3Editor*>(editor.get());
    check(hosted != nullptr);

    if (hosted == nullptr || !connection.isValid())
        return;

    const auto size = hosted->initialSize();
    auto& surface = static_cast<eacp::Graphics::NativeChildSurface&>(hosted->view());

    {
        const auto dpi = ScopedDpi(connection, 192);
        auto embedding = Embedding(*hosted);
        check(embedding.visible);

        // Not polled through backingScale(), which would take the change from
        // the editor's onBackingScaleChanged.
        const auto handle = surfaceOf(*hosted);
        check(pumpUntil([&] { return connection.childCount(handle) == 1; }));

        const auto pluginWindow = connection.firstChild(handle);
        check(pumpUntil(
            [&]
            { return sameSize(connection.sizeOf(pluginWindow), times(size, 2)); }));

        auto bounds = surface.getLocalBounds();
        check(static_cast<int>(bounds.w) == size.width
              && static_cast<int>(bounds.h) == size.height);
        check(sameSize(hosted->initialSize(), size));

        connection.setResources(dpi.original);
        check(pumpUntil(
            [&] { return sameSize(connection.sizeOf(pluginWindow), size); }));
        check(sameSize(hosted->initialSize(), size));
        check(surface.backingScale() == 1.f);
    }
};

// HostFixtureInstrument's editor is fixed-size and asks for resizedSize itself
// once shown, on a later turn of its own copy of eacp's loop, which only the
// host's run loop or the host's loop drives in a process like this one.
auto tOwnResize = test("HostedEditor/aFixedSizeVST3ViewResizesItself") = []
{
    if (!hasDisplay())
        return;

    const auto formats = DefaultFormats();
    auto loaded = LoadResult();
    auto editor = editorFor(formats, "HostFixtureInstrument", loaded);
    auto* hosted = dynamic_cast<HostedVST3Editor*>(editor.get());
    check(hosted != nullptr);

    if (hosted == nullptr)
        return;

    check(!hosted->constraints().resizable);
    check(sameSize(hosted->initialSize(), HostFixtures::initialSize));

    auto embedding = Embedding(*hosted);
    check(embedding.visible);
    check(pumpUntil(
        [&]
        {
            for (auto& size: embedding.host.requests)
                if (sameSize(size, HostFixtures::resizedSize))
                    return true;

            return false;
        }));

    check(sameSize(hosted->initialSize(), HostFixtures::resizedSize));
};
} // namespace
} // namespace HostedEditorTests
