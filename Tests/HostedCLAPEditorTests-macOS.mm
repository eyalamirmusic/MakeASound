// Hosted CLAP editors on macOS: the CLAP fixtures' GUIs parented (cocoa) into an
// eacp view embedded in a plain NSView, at the size they report, resized by the
// host, resizing themselves through request_resize, and removed; and the gui
// stub (CLAPGuiStub.h) attached and removed in the spec's order, its own resize
// reaching the container without an echo, show and hide on request, attached
// again, and a GUI it lost while attached left alone.

#include "CLAPGuiStub.h"
#include "HostFixtures/InstrumentEditor.h"

#include <MakeASound/Hosting/MakeASoundHosting.h>
#include <MakeASound/Hosting/HostedEditor.h>
#include <MakeASound/Hosting/CLAP/HostedCLAPEditor.h>

#include <eacp/Graphics/Window/EmbeddedView.h>
#include <eacp/Graphics/Window/NativeChildSurface.h>

#include <eacp/Core/Threads/EventLoop.h>

#include <NanoTest/NanoTest.h>

#import <Cocoa/Cocoa.h>

#include <filesystem>
#include <string>
#include <vector>

namespace HostedCLAPEditorTestsMacOS
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

bool sameSize(EditorSize a, EditorSize b)
{
    return a.width == b.width && a.height == b.height;
}

EditorSize sizeOfView(eacp::Graphics::View& view)
{
    auto bounds = view.getLocalBounds();
    return {static_cast<int>(bounds.w), static_cast<int>(bounds.h)};
}

// A view hears its new bounds (resized()) in AppKit's layout pass, which a
// window runs on a later turn of the loop and a plain NSView, as here, never
// does: the pass is run here, as a window would.
void layOut(eacp::Graphics::View& view)
{
    auto* native = (__bridge NSView*) view.getHandle();
    [native setNeedsLayout:YES];
    [native layoutSubtreeIfNeeded];
}

void setSize(eacp::Graphics::View& view, EditorSize size)
{
    view.setBounds(
        {0.f, 0.f, static_cast<float>(size.width), static_cast<float>(size.height)});
    layOut(view);
}

template <typename Predicate>
bool pumpUntil(Predicate ready)
{
    return eacp::Threads::runEventLoopUntil(ready, eacp::Time::MS {3000});
}

void pumpBriefly(eacp::Graphics::View& view)
{
    eacp::Threads::runEventLoopUntil([] { return false; }, eacp::Time::MS {100});
    layOut(view);
}

// The host side of an editor: grants every resize, remembers it, and resizes the
// editor's view to it as a window would.
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

// The NSView the editor's surface parents the plugin's view into, or nil.
NSView* surfaceOf(Editor& editor)
{
    auto& surface = static_cast<eacp::Graphics::NativeChildSurface&>(editor.view());
    return (__bridge NSView*) surface.getNativeParentHandle();
}

// An editor's view embedded in a plain NSView, as a plugin window would hold it.
struct Embedding
{
    explicit Embedding(Editor& editorToUse)
        : editor(editorToUse)
    {
        auto size = editor.initialSize();
        auto frame = NSMakeRect(0, 0, size.width, size.height);
        parent = [[NSView alloc] initWithFrame:frame];
        embedded = EA::makeOwned<eacp::Graphics::EmbeddedView>(
            (__bridge void*) parent,
            eacp::Graphics::EmbeddedViewOptions {size.width, size.height});
        embedded->setContentView(editor.view());
        host.editor = &editor;
        editor.setHost(&host);
        editor.onAttached();
    }

    ~Embedding()
    {
        editor.onRemoved();
        editor.setHost(nullptr);
    }

    // What the plugin parented into the surface.
    NSArray<NSView*>* pluginViews()
    {
        auto* native = surfaceOf(editor);
        return native != nil ? native.subviews : @[];
    }

    EditorSize pluginViewSize()
    {
        auto* views = pluginViews();

        if (views.count != 1)
            return {0, 0};

        auto frame = views[0].frame.size;
        return {static_cast<int>(frame.width), static_cast<int>(frame.height)};
    }

    Editor& editor;
    FollowingHost host;
    NSView* parent = nil;
    OwningPointer<eacp::Graphics::EmbeddedView> embedded;
};

LoadResult loadFixture(const FormatManager& formats, const char* name)
{
    auto scan = formats.scanTarget(fixture(name)).scan;
    check(scan.classes.size() == 1);

    if (scan.classes.empty())
        return {};

    return formats.load(scan.classes[0], {.editors = hostedEditors()});
}

// HostFixtureInstrument's editor is fixed-size and asks for resizedSize itself
// once shown, through request_resize; the host follows, the container with it,
// and the plugin keeps the size it asked for.
auto tInstrument =
    test("HostedCLAPEditor/aFixedSizeCLAPGuiAttachesResizesItselfAndDetaches") = []
{
    @autoreleasepool
    {
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

        {
            auto embedding = Embedding(*hosted);
            check(hosted->isGuiAttached());
            check(embedding.pluginViews().count == 1);
            check(sameSize(embedding.pluginViewSize(), HostFixtures::initialSize));

            check(eacp::Threads::runEventLoopUntil(
                [&] { return embedding.host.received(HostFixtures::resizedSize); },
                eacp::Time::MS {3000}));

            check(sameSize(sizeOfView(hosted->view()), HostFixtures::resizedSize));
            check(sameSize(embedding.pluginViewSize(), HostFixtures::resizedSize));
            check(sameSize(hosted->initialSize(), HostFixtures::resizedSize));
        }

        // Removed with the embedding: hidden, destroyed, and gone from the surface.
        check(!hosted->isGuiCreated());
        auto* native = surfaceOf(*hosted);
        check(native == nil || native.subviews.count == 0);
    }
};

auto tEffect = test("HostedCLAPEditor/aCLAPGuiFollowsTheHostsResize") = []
{
    @autoreleasepool
    {
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
        auto embedding = Embedding(*hosted);
        check(embedding.pluginViews().count == 1);
        check(sameSize(embedding.pluginViewSize(), size));
        check(hosted->constraints().resizable);

        auto target = hosted->constrainSize({size.width + 40, size.height + 30});
        setSize(hosted->view(), target);
        check(pumpUntil([&] { return sameSize(hosted->initialSize(), target); }));
        check(sameSize(embedding.pluginViewSize(), target));
    }
};

// get_size, set_parent, show; a host resize as set_size; the plugin's own resize
// reaching the container with no set_size echoing it; show and hide on request;
// hide and destroy on removal, and the whole of it again on a second attach.
auto tStubAttach =
    test("HostedCLAPEditor/aGuiAttachesAndDetachesInTheSpecsOrder") = []
{
    @autoreleasepool
    {
        auto stub = CLAPGuiStub::Plugin();
        auto plugin = CLAPGuiStub::load(stub, hostedEditors());

        if (plugin == nullptr)
            return;

        auto editor = plugin->createEditor();
        auto* hosted = dynamic_cast<HostedCLAPEditor*>(editor.get());
        check(hosted != nullptr);

        if (hosted == nullptr)
            return;

        for (auto round = 0; round < 2; ++round)
        {
            auto mark = stub.calls.size();

            {
                auto embedding = Embedding(*hosted);
                check(hosted->isGuiAttached());
                check(stub.parented && stub.visible);

                // Created with the editor the first time, anew the second.
                auto expected =
                    round == 0
                        ? std::vector<std::string> {"set_parent", "show"}
                        : std::vector<std::string> {"create", "set_parent", "show"};
                check(stub.lifecycleSince(mark) == expected);
                check(stub.precededBy(mark, "set_parent", "get_size"));

                setSize(hosted->view(), {360, 240});
                check(pumpUntil(
                    [&]
                    {
                        return !stub.sizesSet.empty()
                               && stub.sizesSet.back().is(360, 240);
                    }));

                // Taken, with no set_size echoing it once AppKit lays it out.
                auto setBefore = stub.sizesSet.size();
                check(stub.requestResize(420, 280));
                check(sameSize(sizeOfView(hosted->view()), {420, 280}));
                pumpBriefly(hosted->view());
                check(stub.sizesSet.size() == setBefore);

                check(stub.requestHide() && !stub.visible);
                check(stub.requestShow() && stub.visible);

                mark = stub.calls.size();
            }

            check(stub.callsSince(mark)
                  == std::vector<std::string> {"hide", "destroy"});
            check(!hosted->isGuiCreated());
        }

        editor.reset();
        plugin.reset();
        check(CLAPGuiStub::isClean(stub));
    }
};

// Lost while attached: destroyed at once, and nothing more on removal.
auto tStubLost = test("HostedCLAPEditor/aGuiLostWhileAttachedIsLeftAlone") = []
{
    @autoreleasepool
    {
        auto stub = CLAPGuiStub::Plugin();
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

            // Destroyed once the plugin is out of its call, by the service.
            stub.runInside([&] { stub.closed(true); });
            check(!hosted->isGuiAttached() && !hosted->isGuiCreated());
            check(stub.count("destroy") == 0);
            check(pumpUntil([&] { return stub.count("destroy") == 1; }));
            check(stub.count("hide") == 0);

            auto mark = stub.calls.size();
            setSize(hosted->view(), {500, 400});
            pumpBriefly(hosted->view());
            check(!stub.requestShow());
            check(stub.calls.size() == mark);
        }

        check(stub.count("destroy") == 1 && stub.count("hide") == 0);

        editor.reset();
        plugin.reset();
        check(CLAPGuiStub::isClean(stub));
    }
};

// Lost from inside set_parent or show, which then answer yes: the GUI is
// neither attached nor shown, takes no further call, and is destroyed after
// the plugin's call returns, by the service.
auto tStubLostInside =
    test("HostedCLAPEditor/aGuiLostInsideSetParentOrShowIsDestroyedLater") = []
{
    for (auto inShow: {false, true})
    {
        @autoreleasepool
        {
            auto stub = CLAPGuiStub::Plugin();
            auto plugin = CLAPGuiStub::load(stub, hostedEditors());

            if (plugin == nullptr)
                return;

            auto lose = [&] { stub.closed(true); };

            if (inShow)
                stub.duringShow = lose;
            else
                stub.duringSetParent = lose;

            auto editor = plugin->createEditor();
            auto* hosted = dynamic_cast<HostedCLAPEditor*>(editor.get());
            check(hosted != nullptr);

            if (hosted == nullptr)
                return;

            {
                auto embedding = Embedding(*hosted);
                check(!hosted->isGuiAttached() && !hosted->isGuiCreated());
                check(stub.count("destroy") == 0);
                check(stub.count("show") == (inShow ? 1 : 0));

                auto mark = stub.calls.size();
                check(!stub.requestShow() && !stub.requestHide());
                check(stub.calls.size() == mark);

                check(pumpUntil([&] { return stub.count("destroy") == 1; }));
                check(stub.callsSince(mark) == std::vector<std::string> {"destroy"});
            }

            check(stub.count("destroy") == 1 && stub.count("hide") == 0);

            editor.reset();
            plugin.reset();
            check(CLAPGuiStub::isClean(stub));
        }
    }
};

// The scale every editor converts at, for as long as it lives.
struct ScaleOverride
{
    explicit ScaleOverride(float scale)
    {
        HostedCLAPEditor::overrideScaleForTesting(scale);
    }

    ~ScaleOverride() { HostedCLAPEditor::overrideScaleForTesting(0.f); }
};

// At 150%, a size in pixels does not survive the trip to points and back (601
// px is 401 pt, which is 602 px): the plugin's own size is never echoed back to
// it, and a fixed-size GUI is never sent set_size at all.
auto tFractionalScale =
    test("HostedCLAPEditor/aFractionalScaleSendsNoEchoAndNoSetSizeToAFixedGui") = []
{
    @autoreleasepool
    {
        const auto scale = ScaleOverride(1.5f);
        auto stub = CLAPGuiStub::Plugin();
        stub.size = {601, 401};
        stub.resizable = false;
        auto plugin = CLAPGuiStub::load(stub, hostedEditors());

        if (plugin == nullptr)
            return;

        auto editor = plugin->createEditor();
        auto* hosted = dynamic_cast<HostedCLAPEditor*>(editor.get());
        check(hosted != nullptr);

        if (hosted == nullptr)
            return;

        check(sameSize(hosted->initialSize(), {401, 267}));

        {
            auto embedding = Embedding(*hosted);
            check(hosted->isGuiAttached());
            pumpBriefly(hosted->view());
            check(stub.sizesSet.empty());

            // Fixed-size, resizing itself: the host follows, and sends nothing.
            check(stub.requestResize(751, 501));
            check(sameSize(sizeOfView(hosted->view()), {501, 334}));
            pumpBriefly(hosted->view());
            check(stub.sizesSet.empty());

            // Resizable: its own size is not echoed, the host's is sent.
            stub.resizable = true;
            check(stub.requestResize(601, 401));
            check(sameSize(sizeOfView(hosted->view()), {401, 267}));
            pumpBriefly(hosted->view());
            check(stub.sizesSet.empty());

            setSize(hosted->view(), {360, 240});
            check(pumpUntil([&]
                            { return !stub.sizesSet.empty()
                                     && stub.sizesSet.back().is(540, 360); }));
            check(stub.sizesSet.size() == 1);
        }

        editor.reset();
        plugin.reset();
        check(CLAPGuiStub::isClean(stub));
    }
};

// Removed, an editor holds the GUI no more: a second editor creates one, and
// the first, attached again meanwhile, stays empty rather than make another.
auto tReattachBlocked =
    test("HostedCLAPEditor/aRemovedEditorAttachedAgainYieldsToAnother") = []
{
    @autoreleasepool
    {
        auto stub = CLAPGuiStub::Plugin();
        auto plugin = CLAPGuiStub::load(stub, hostedEditors());

        if (plugin == nullptr)
            return;

        auto first = plugin->createEditor();
        auto* hosted = dynamic_cast<HostedCLAPEditor*>(first.get());
        check(hosted != nullptr);

        if (hosted == nullptr)
            return;

        {
            auto embedding = Embedding(*hosted);
            check(hosted->isGuiAttached());
        }

        auto second = plugin->createEditor();
        check(second != nullptr);
        check(stub.count("create") == 2);

        {
            auto embedding = Embedding(*hosted);
            check(!hosted->isGuiAttached() && !hosted->isGuiCreated());
            check(stub.count("create") == 2);
        }

        second.reset();

        // Free again: attaching creates the GUI anew.
        {
            auto embedding = Embedding(*hosted);
            check(hosted->isGuiAttached());
            check(stub.count("create") == 3);
        }

        first.reset();
        plugin.reset();
        check(CLAPGuiStub::isClean(stub));
    }
};
} // namespace
} // namespace HostedCLAPEditorTestsMacOS
