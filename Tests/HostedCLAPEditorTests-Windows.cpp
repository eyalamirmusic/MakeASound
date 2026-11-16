// Hosted CLAP editors on Windows: the CLAP fixtures' GUIs parented (win32) into
// an eacp NativeChildSurface in a window of the test's, resized by the host and
// resizing themselves through request_resize, in physical pixels at the window's
// DPI, and removed; the gui stub (CLAPGuiStub.h) attached and removed in the
// spec's order, told the window's own scale through set_scale and every size in
// pixels; and a scale change reported by the surface reaching the GUI as
// set_scale and set_size. The windows need a desktop: run in a logged-in session,
// not as a service (session 0 has no compositor).

#include "CLAPGuiStub.h"
#include "HostFixtures/InstrumentEditor.h"

#include <MakeASound/Hosting/MakeASoundHosting.h>
#include <MakeASound/Hosting/HostedEditor.h>
#include <MakeASound/Hosting/CLAP/HostedCLAPEditor.h>

#include <eacp/Core/App/AppEnvironment.h>
#include <eacp/Core/Threads/EventLoop.h>
#include <eacp/Graphics/Window/NativeChildSurface.h>
#include <eacp/Graphics/Window/Window.h>

#include <NanoTest/NanoTest.h>

#include <eacp/Core/Utils/WinInclude.h>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace HostedCLAPEditorTestsWindows
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

template <typename Predicate>
bool pumpUntil(Predicate ready)
{
    return eacp::Threads::runEventLoopUntil(ready, eacp::Time::MS {3000});
}

void pumpBriefly()
{
    eacp::Threads::runEventLoopUntil([] { return false; }, eacp::Time::MS {100});
}

// False, and the case skips, where eacp opens no windows. Otherwise the loop
// runs once first, as a host app's does before it makes an editor or opens a
// window: that is what makes the process per-monitor DPI aware, without which
// every window is 96 DPI to the OS.
bool startHost()
{
    if (eacp::Apps::getAppEnvironment().headless)
        return false;

    eacp::Threads::runEventLoopFor(eacp::Time::MS {1});
    return true;
}

bool sameSize(EditorSize a, EditorSize b)
{
    return a.width == b.width && a.height == b.height;
}

bool sameScale(double a, double b)
{
    return std::abs(a - b) < 1e-4;
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

uint32_t pixels(int points, float scale)
{
    return static_cast<uint32_t>(std::lround(static_cast<float>(points) * scale));
}

EditorSize inPixels(EditorSize points, float scale)
{
    return {static_cast<int>(pixels(points.width, scale)),
            static_cast<int>(pixels(points.height, scale))};
}

eacp::Graphics::NativeChildSurface& surfaceViewOf(Editor& editor)
{
    return static_cast<eacp::Graphics::NativeChildSurface&>(editor.view());
}

// The child window the editor's surface parents the plugin's window into.
HWND surfaceOf(Editor& editor)
{
    return static_cast<HWND>(surfaceViewOf(editor).getNativeParentHandle());
}

// Physical pixels per point of the window, as the OS has it.
float scaleOf(HWND window)
{
    auto dpi = window != nullptr ? GetDpiForWindow(window) : 0u;
    return dpi > 0 ? static_cast<float>(dpi) / 96.f : 1.f;
}

HWND firstChild(HWND window)
{
    return window != nullptr && IsWindow(window) ? GetWindow(window, GW_CHILD)
                                                 : nullptr;
}

int childCount(HWND window)
{
    auto count = 0;

    for (auto* child = firstChild(window); child != nullptr;
         child = GetWindow(child, GW_HWNDNEXT))
        ++count;

    return count;
}

EditorSize pixelSizeOf(HWND window)
{
    auto rect = RECT {};

    if (window == nullptr || !GetWindowRect(window, &rect))
        return {0, 0};

    return {static_cast<int>(rect.right - rect.left),
            static_cast<int>(rect.bottom - rect.top)};
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

OwningPointer<eacp::Graphics::Window> openWindow(EditorSize size)
{
    auto options = eacp::Graphics::WindowOptions();
    options.width = size.width;
    options.height = size.height;
    options.title = "MakeASound hosted CLAP editor tests";
    options.isPrimary = false;
    return EA::makeOwned<eacp::Graphics::Window>(options);
}

// An editor's view as the content of a top-level window of eacp's: one of its
// own, or `existing`, which outlives it.
//
// An editor attached again goes back into the same window: on Windows, eacp's
// NativeChildSurface keeps its child HWND after the top-level window holding
// it is destroyed (DestroyWindow takes the children with it), so a surface
// moved to a second window would hand out a dead handle.
struct Embedding
{
    explicit Embedding(Editor& editorToUse,
                       eacp::Graphics::Window* existing = nullptr)
        : editor(editorToUse)
    {
        if (existing == nullptr)
        {
            ownWindow = openWindow(editor.initialSize());
            existing = ownWindow.get();
        }

        window = existing;
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
    OwningPointer<eacp::Graphics::Window> ownWindow;
    eacp::Graphics::Window* window = nullptr;
    bool visible = false;
};

LoadResult loadFixture(const FormatManager& formats, const char* name)
{
    auto scan = formats.scanTarget(fixture(name)).scan;
    check(scan.classes.size() == 1);

    if (scan.classes.empty())
        return {};

    return formats.load(scan.classes[0], {.editors = hostedEditors()});
}

// The scale every editor converts at, for as long as it lives.
struct ScaleOverride
{
    explicit ScaleOverride(float scale)
    {
        HostedCLAPEditor::overrideScaleForTesting(scale);
    }

    ~ScaleOverride() { HostedCLAPEditor::overrideScaleForTesting(0.f); }

    ScaleOverride(const ScaleOverride&) = delete;
    ScaleOverride& operator=(const ScaleOverride&) = delete;
};

// HostFixtureEffect's GUI, a window of its own copy of eacp, becomes a child of
// the surface's window, and follows the host's resize in pixels.
auto tEffect =
    test("HostedCLAPEditor/aCLAPGuiAttachesFollowsTheHostAndDetaches") = []
{
    if (!startHost())
        return;

    const auto formats = DefaultFormats();
    auto loaded = loadFixture(formats, "HostFixtureEffect");
    check(loaded.plugin != nullptr);

    if (loaded.plugin == nullptr)
        return;

    auto editor = loaded.plugin->createEditor();
    auto* hosted = dynamic_cast<HostedCLAPEditor*>(editor.get());
    check(hosted != nullptr);

    if (hosted == nullptr)
        return;

    auto size = hosted->initialSize();
    check(size.width > 0 && size.height > 0);
    check(hosted->constraints().resizable);

    auto surface = HWND {};

    {
        auto embedding = Embedding(*hosted);
        check(embedding.visible);
        check(hosted->isGuiAttached());

        surface = surfaceOf(*hosted);
        check(surface != nullptr);
        check((GetWindowLongW(surface, GWL_STYLE) & WS_CHILD) != 0);
        check(pumpUntil([&] { return childCount(surface) == 1; }));

        auto* pluginWindow = firstChild(surface);
        check(pluginWindow != nullptr && GetParent(pluginWindow) == surface);

        auto scale = scaleOf(surface);
        check(pumpUntil(
            [&]
            {
                return sameSize(pixelSizeOf(pluginWindow),
                                inPixels(sizeOfView(hosted->view()), scale));
            }));

        auto target = hosted->constrainSize({size.width + 40, size.height + 30});
        check(!sameSize(target, size));
        setSize(hosted->view(), target);

        check(pumpUntil(
            [&]
            {
                return sameSize(pixelSizeOf(pluginWindow), inPixels(target, scale));
            }));
        check(sameSize(pixelSizeOf(surface), inPixels(target, scale)));
    }

    // Removed with the embedding: hidden, destroyed, its window gone.
    check(!hosted->isGuiCreated());
    check(pumpUntil([&] { return childCount(surface) == 0; }));
};

// HostFixtureInstrument's editor is fixed-size and asks for resizedSize itself
// once shown, on a later turn of the loop; the host follows, the surface with
// it, and the plugin's window keeps the size it asked for.
auto tOwnResize =
    test("HostedCLAPEditor/aFixedSizeCLAPGuiAttachesResizesItselfAndDetaches") = []
{
    if (!startHost())
        return;

    const auto formats = DefaultFormats();
    auto loaded = loadFixture(formats, "HostFixtureInstrument");
    check(loaded.plugin != nullptr);

    if (loaded.plugin == nullptr)
        return;

    auto editor = loaded.plugin->createEditor();
    auto* hosted = dynamic_cast<HostedCLAPEditor*>(editor.get());
    check(hosted != nullptr);

    if (hosted == nullptr)
        return;

    check(!hosted->constraints().resizable);
    check(sameSize(hosted->initialSize(), HostFixtures::initialSize));

    auto surface = HWND {};

    {
        auto embedding = Embedding(*hosted);
        check(embedding.visible);
        check(hosted->isGuiAttached());

        surface = surfaceOf(*hosted);
        check(pumpUntil([&] { return childCount(surface) == 1; }));

        check(pumpUntil(
            [&] { return embedding.host.received(HostFixtures::resizedSize); }));
        check(sameSize(sizeOfView(hosted->view()), HostFixtures::resizedSize));
        check(sameSize(hosted->initialSize(), HostFixtures::resizedSize));

        auto pixelsWanted = inPixels(HostFixtures::resizedSize, scaleOf(surface));
        check(sameSize(pixelSizeOf(surface), pixelsWanted));
        check(pumpUntil(
            [&]
            { return sameSize(pixelSizeOf(firstChild(surface)), pixelsWanted); }));
    }

    check(!hosted->isGuiCreated());
    check(pumpUntil([&] { return childCount(surface) == 0; }));
};

// is_api_supported, create and set_scale with the editor, then set_scale again
// if the window's DPI differs, get_size, set_parent, show; a host resize as
// set_size in pixels; the plugin's own resize reaching the container with no
// set_size echoing it; show and hide on request; hide and destroy on removal,
// and the whole of it again on a second attach.
auto tStubAttach =
    test("HostedCLAPEditor/aGuiAttachesAndDetachesInTheSpecsOrder") = []
{
    if (!startHost())
        return;

    auto stub = CLAPGuiStub::Plugin();
    stub.size = {800, 600};
    auto plugin = CLAPGuiStub::load(stub, hostedEditors());

    if (plugin == nullptr)
        return;

    auto editor = plugin->createEditor();
    auto* hosted = dynamic_cast<HostedCLAPEditor*>(editor.get());
    check(hosted != nullptr);

    if (hosted == nullptr)
        return;

    check(stub.count("set_scale") == 1);

    auto window = openWindow(hosted->initialSize());

    for (auto round = 0; round < 2; ++round)
    {
        auto mark = stub.calls.size();
        auto scaleBefore = stub.scale;

        {
            auto embedding = Embedding(*hosted, window.get());
            check(embedding.visible);
            check(hosted->isGuiAttached());
            check(stub.parented && stub.visible);
            check(stub.api == CLAP_WINDOW_API_WIN32);

            // Told the scale of the window it is parented into.
            auto surface = surfaceOf(*hosted);
            auto scale = scaleOf(surface);
            check(surface != nullptr);
            check(sameScale(stub.scale, scale));

            if (round == 0)
                std::fprintf(stderr,
                             "window DPI scale %.2f, system DPI scale %.2f\n",
                             scale,
                             static_cast<float>(GetDpiForSystem()) / 96.f);

            // Created with the editor the first time, anew the second, and
            // scaled again only when the window's DPI is not the one it had.
            auto expected = std::vector<std::string>();

            if (round == 1)
                expected = {"create", "set_scale"};
            else if (!sameScale(scaleBefore, scale))
                expected = {"set_scale"};

            expected.push_back("set_parent");
            expected.push_back("show");
            check(stub.lifecycleSince(mark) == expected);
            check(stub.precededBy(mark, "set_parent", "get_size"));

            // The surface is the size the plugin reported, in pixels.
            check(sameSize(pixelSizeOf(surface),
                           {static_cast<int>(stub.size.width),
                            static_cast<int>(stub.size.height)}));

            setSize(hosted->view(), {360, 240});
            check(pumpUntil(
                [&]
                {
                    return !stub.sizesSet.empty()
                           && stub.sizesSet.back().is(pixels(360, scale),
                                                      pixels(240, scale));
                }));
            check(sameSize(pixelSizeOf(surface), inPixels({360, 240}, scale)));

            // Taken, with no set_size echoing it however late the surface hears.
            auto setBefore = stub.sizesSet.size();
            check(stub.requestResize(pixels(420, scale), pixels(280, scale)));
            check(sameSize(sizeOfView(hosted->view()), {420, 280}));
            pumpBriefly();
            check(stub.sizesSet.size() == setBefore);

            check(stub.requestHide() && !stub.visible);
            check(stub.requestShow() && stub.visible);

            mark = stub.calls.size();
        }

        check(stub.callsSince(mark) == std::vector<std::string> {"hide", "destroy"});
        check(!hosted->isGuiCreated());
    }

    // Before the editor, whose view is the window's content.
    window.reset();
    editor.reset();
    plugin.reset();
    check(CLAPGuiStub::isClean(stub));
};

// The scale moving without a resize: what the surface reports (a window moved
// to a display of another DPI, which eacp hears as WM_DPICHANGED_AFTERPARENT)
// reaches the GUI as set_scale, then set_size for the same points in the new
// pixels. One display has one DPI, so the new one is the test's override and the
// report is the surface's callback; the real message at an unchanged DPI sends
// the GUI nothing.
auto tScaleChange = test("HostedCLAPEditor/aGuiFollowsTheWindowsScale") = []
{
    if (!startHost())
        return;

    auto stub = CLAPGuiStub::Plugin();
    stub.size = {800, 600};
    auto plugin = CLAPGuiStub::load(stub, hostedEditors());

    if (plugin == nullptr)
        return;

    auto editor = plugin->createEditor();
    auto* hosted = dynamic_cast<HostedCLAPEditor*>(editor.get());
    check(hosted != nullptr);

    if (hosted == nullptr)
        return;

    {
        auto embedding = Embedding(*hosted);
        check(hosted->isGuiAttached());

        auto surface = surfaceOf(*hosted);
        auto scale = scaleOf(surface);
        check(sameScale(stub.scale, scale));
        auto points = sizeOfView(hosted->view());

        // The DPI is what it was: placed again, and nothing for the GUI.
        auto mark = stub.calls.size();
        constexpr auto dpiChangedAfterParent = UINT {0x02E3};
        SendMessageW(surface, dpiChangedAfterParent, 0, 0);
        pumpBriefly();
        check(stub.callsSince(mark).empty());
        check(sameSize(pixelSizeOf(surface), inPixels(points, scale)));

        {
            auto moved = scale + 1.f;
            auto overridden = ScaleOverride(moved);
            surfaceViewOf(*hosted).onBackingScaleChanged(moved);

            check(stub.callsSince(mark)
                  == std::vector<std::string> {"set_scale", "set_size"});
            check(sameScale(stub.scale, moved));
            check(!stub.sizesSet.empty()
                  && stub.sizesSet.back().is(pixels(points.width, moved),
                                             pixels(points.height, moved)));
            check(sameSize(sizeOfView(hosted->view()), points));

            // A host resize at the new scale is in its pixels.
            setSize(hosted->view(), {400, 300});
            check(pumpUntil(
                [&]
                {
                    return stub.sizesSet.back().is(pixels(400, moved),
                                                   pixels(300, moved));
                }));

            // The plugin's own resize is read at it too.
            check(stub.requestResize(pixels(500, moved), pixels(350, moved)));
            check(sameSize(sizeOfView(hosted->view()), {500, 350}));
        }

        // Back on the window's own DPI.
        mark = stub.calls.size();
        surfaceViewOf(*hosted).onBackingScaleChanged(scale);
        check(stub.callsSince(mark)
              == std::vector<std::string> {"set_scale", "set_size"});
        check(sameScale(stub.scale, scale));
        check(stub.sizesSet.back().is(pixels(500, scale), pixels(350, scale)));
    }

    editor.reset();
    plugin.reset();
    check(CLAPGuiStub::isClean(stub));
};
} // namespace
} // namespace HostedCLAPEditorTestsWindows
