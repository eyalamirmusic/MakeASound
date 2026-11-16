// Hosted CLAP editors on Linux: the CLAP fixtures' GUIs parented (x11) into an
// eacp NativeChildSurface in an X11 window of the test's, resized by the host,
// resizing themselves through request_resize, and removed; the gui stub
// (CLAPGuiStub.h) attached and removed in the spec's order; and an editor with
// no X11 window to parent into. The fixtures' GUIs run their own copy of eacp's
// loop over the host's clap.posix-fd-support and clap.timer-support. The window
// cases need an X server and eacp's X11 backend (the EACP_WINDOW_SYSTEM=x11
// environment variable, as eacp's Scripts/with-xvfb sets) and skip without one,
// or fail when EACP_REQUIRE_DISPLAY=1. What they read back comes from the
// server, over a connection of the test's own.

#include "CLAPGuiStub.h"
#include "HostFixtures/InstrumentEditor.h"

#include <MakeASound/Hosting/MakeASoundHosting.h>
#include <MakeASound/Hosting/HostedEditor.h>
#include <MakeASound/Hosting/CLAP/HostedCLAPEditor.h>

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
#include <vector>

namespace HostedCLAPEditorTestsLinux
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
    return (std::filesystem::path(MAKEASOUND_HOST_FIXTURE_CLAP_DIR)
            / (std::string(name) + ".clap"))
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

bool sameSize(EditorSize a, EditorSize b)
{
    return a.width == b.width && a.height == b.height;
}

void setSize(eacp::Graphics::View& view, EditorSize size)
{
    view.setBounds(
        {0.f, 0.f, static_cast<float>(size.width), static_cast<float>(size.height)});
}

EditorSize sizeOfView(eacp::Graphics::View& view)
{
    auto bounds = view.getLocalBounds();
    return {static_cast<int>(bounds.w), static_cast<int>(bounds.h)};
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

    xcb_connection_t* xcb;
};

xcb_window_t surfaceOf(Editor& editor)
{
    auto& surface = static_cast<eacp::Graphics::NativeChildSurface&>(editor.view());
    return static_cast<xcb_window_t>(
        reinterpret_cast<std::uintptr_t>(surface.getNativeParentHandle()));
}

// Grants every resize, remembers it, and resizes the editor's view to it.
struct FollowingHost : EditorHost
{
    bool resizeEditor(EditorSize size) override
    {
        requests.push_back(size);

        if (editor != nullptr)
            setSize(editor->view(), size);

        return true;
    }

    bool drawsResizeHandle() const override { return false; }

    bool received(EditorSize size) const
    {
        for (auto& request: requests)
            if (sameSize(request, size))
                return true;

        return false;
    }

    Editor* editor = nullptr;
    std::vector<EditorSize> requests;
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
        options.title = "MakeASound hosted CLAP editor tests";
        options.isPrimary = false;

        window = EA::makeOwned<eacp::Graphics::Window>(options);
        window->setContentView(editor.view());
        visible = pumpUntil([this] { return window->isVisible(); });

        host.editor = &editor;
        editor.setHost(&host);
        editor.onAttached();
    }

    ~Embedding()
    {
        editor.onRemoved();
        editor.setHost(nullptr);
    }

    Editor& editor;
    FollowingHost host;
    OwningPointer<eacp::Graphics::Window> window;
    bool visible = false;
};

OwningPointer<Editor>
    editorFor(const FormatManager& formats, const char* name, LoadResult& loaded)
{
    auto scan = formats.scanTarget(fixture(name)).scan;
    check(scan.classes.size() == 1);

    if (scan.classes.empty())
        return {};

    loaded = formats.load(scan.classes[0], {.editors = hostedEditors()});
    return loaded.plugin != nullptr ? loaded.plugin->createEditor() : nullptr;
}

// No X11 window (headless here; a Wayland one alike): nothing to parent into, so
// the GUI is created but never parented, and nothing crashes.
auto tNoWindow = test("HostedCLAPEditor/withoutAnX11WindowTheGuiIsNotParented") = []
{
    const auto formats = DefaultFormats();
    auto loaded = LoadResult();
    auto editor = editorFor(formats, "HostFixtureEffect", loaded);
    auto* hosted = dynamic_cast<HostedCLAPEditor*>(editor.get());
    check(hosted != nullptr);

    if (hosted == nullptr)
        return;

    hosted->onAttached();
    check(surfaceOf(*hosted) == XCB_NONE);
    check(!hosted->isGuiAttached());
    hosted->onRemoved();
};

auto tEffect =
    test("HostedCLAPEditor/aCLAPGuiAttachesFollowsTheHostAndDetaches") = []
{
    if (!hasDisplay())
        return;

    const auto connection = Connection();
    check(connection.isValid());

    const auto formats = DefaultFormats();
    auto loaded = LoadResult();
    auto editor = editorFor(formats, "HostFixtureEffect", loaded);
    auto* hosted = dynamic_cast<HostedCLAPEditor*>(editor.get());
    check(hosted != nullptr);

    if (hosted == nullptr || !connection.isValid())
        return;

    auto size = hosted->initialSize();
    check(size.width > 0 && size.height > 0);

    auto surface = xcb_window_t {XCB_NONE};

    {
        auto embedding = Embedding(*hosted);
        check(embedding.visible);
        check(hosted->isGuiAttached());

        surface = surfaceOf(*hosted);
        check(surface != XCB_NONE);
        check(pumpUntil([&] { return connection.childCount(surface) == 1; }));

        const auto pluginWindow = connection.firstChild(surface);

        // At scale 1 on a plain Xvfb, pixels are points.
        if (hosted->constraints().resizable && pluginWindow != XCB_NONE)
        {
            auto target = hosted->constrainSize({size.width + 40, size.height + 30});
            setSize(hosted->view(), target);

            check(pumpUntil(
                [&] { return sameSize(connection.sizeOf(pluginWindow), target); }));
        }
    }

    // Removed with the embedding: hidden, destroyed, its window gone.
    check(!hosted->isGuiCreated());
    check(pumpUntil([&] { return connection.childCount(surface) <= 0; }));
};

// HostFixtureInstrument's editor is fixed-size and asks for resizedSize itself
// once shown, on a later turn of its own copy of eacp's loop, which the host's
// fd and timer support drive.
auto tOwnResize = test("HostedCLAPEditor/aFixedSizeCLAPGuiResizesItself") = []
{
    if (!hasDisplay())
        return;

    const auto formats = DefaultFormats();
    auto loaded = LoadResult();
    auto editor = editorFor(formats, "HostFixtureInstrument", loaded);
    auto* hosted = dynamic_cast<HostedCLAPEditor*>(editor.get());
    check(hosted != nullptr);

    if (hosted == nullptr)
        return;

    check(!hosted->constraints().resizable);
    check(sameSize(hosted->initialSize(), HostFixtures::initialSize));

    auto embedding = Embedding(*hosted);
    check(embedding.visible);
    check(pumpUntil([&]
                    { return embedding.host.received(HostFixtures::resizedSize); }));

    check(sameSize(sizeOfView(hosted->view()), HostFixtures::resizedSize));
    check(sameSize(hosted->initialSize(), HostFixtures::resizedSize));
};

auto tStubAttach =
    test("HostedCLAPEditor/aGuiAttachesAndDetachesInTheSpecsOrder") = []
{
    if (!hasDisplay())
        return;

    auto stub = CLAPGuiStub::Plugin();
    auto plugin = CLAPGuiStub::load(stub, hostedEditors());

    if (plugin == nullptr)
        return;

    auto editor = plugin->createEditor();
    auto* hosted = dynamic_cast<HostedCLAPEditor*>(editor.get());
    check(hosted != nullptr);

    if (hosted == nullptr)
        return;

    auto mark = stub.calls.size();

    {
        auto embedding = Embedding(*hosted);
        check(embedding.visible);
        check(hosted->isGuiAttached());
        check(stub.lifecycleSince(mark)
              == std::vector<std::string> {"set_parent", "show"});
        check(stub.precededBy(mark, "set_parent", "get_size"));

        // The parent the stub was handed is the surface's X11 window.
        check(stub.api == CLAP_WINDOW_API_X11);

        // Taken, with no set_size echoing it however late the surface hears.
        auto setBefore = stub.sizesSet.size();
        check(stub.requestResize(420, 280));
        check(sameSize(sizeOfView(hosted->view()), {420, 280}));
        eacp::Threads::runEventLoopUntil([] { return false; }, eacp::Time::MS {100});
        check(stub.sizesSet.size() == setBefore);

        mark = stub.calls.size();
    }

    check(stub.callsSince(mark) == std::vector<std::string> {"hide", "destroy"});

    editor.reset();
    plugin.reset();
    check(CLAPGuiStub::isClean(stub));
};
} // namespace
} // namespace HostedCLAPEditorTestsLinux
