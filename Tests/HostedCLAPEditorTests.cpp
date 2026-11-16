// Hosted CLAP editors without a window, on every platform: the CLAP fixtures'
// GUIs made through hostedEditors(), their sizes and constraints, one GUI per
// instance; and the gui stub (CLAPGuiStub.h), which drives what the fixtures
// cannot: the order the host calls clap.gui in, constraints from resize hints,
// the host side of clap.gui from the main thread and from another one (resize
// requests, resize_hints_changed, closed with was_destroyed), a plugin destroyed
// before its editor, and no calls into a GUI once it is destroyed. Attaching into
// a window is the platform suites' (HostedCLAPEditorTests-<platform>).

#include "CLAPGuiStub.h"
#include "HostFixtures/InstrumentEditor.h"

#include <MakeASound/Hosting/MakeASoundHosting.h>
#include <MakeASound/Hosting/HostedEditor.h>
#include <MakeASound/Hosting/CLAP/HostedCLAPEditor.h>

#include <eacp/Graphics/View/View.h>

#include <NanoTest/NanoTest.h>

#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

namespace HostedCLAPEditorTests
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

LoadResult loadFixture(const FormatManager& formats,
                       const char* name,
                       HostedEditorFactory editors)
{
    auto scan = formats.scanTarget(fixture(name)).scan;
    check(scan.classes.size() == 1);

    if (scan.classes.empty())
        return {};

    return formats.load(scan.classes[0], {.editors = std::move(editors)});
}

HostedCLAPEditor* asCLAP(const OwningPointer<Editor>& editor)
{
    return dynamic_cast<HostedCLAPEditor*>(editor.get());
}

bool sameSize(EditorSize a, EditorSize b)
{
    return a.width == b.width && a.height == b.height;
}

// The host side of an editor: grants (or refuses) every resize and remembers it.
struct RecordingHost : EditorHost
{
    bool resizeEditor(EditorSize size) override
    {
        requests.push_back(size);
        return grant;
    }

    bool drawsResizeHandle() const override { return false; }

    std::vector<EditorSize> requests;
    bool grant = true;
};

// The stub's units and the host's points: physical pixels at the scale the
// plugin was told where the platform tells one, points under cocoa.
struct Units
{
    explicit Units(const CLAPGuiStub::Plugin& stub)
        : scale(stub.scale > 0.0 ? stub.scale : 1.0)
    {
    }

    int points(uint32_t units) const
    {
        return static_cast<int>(std::lround(static_cast<double>(units) / scale));
    }

    uint32_t units(int points) const
    {
        return static_cast<uint32_t>(
            std::lround(static_cast<double>(points) * scale));
    }

    EditorSize size(uint32_t width, uint32_t height) const
    {
        return {points(width), points(height)};
    }

    double scale;
};

auto tFixtures = test("HostedCLAPEditor/theFixturesGuisAreHostedCLAPEditors") = []
{
    const auto formats = DefaultFormats();

    {
        auto loaded = loadFixture(formats, "HostFixtureInstrument", hostedEditors());
        check(loaded.plugin != nullptr);

        if (loaded.plugin == nullptr)
            return;

        auto editor = loaded.plugin->createEditor();
        auto* hosted = asCLAP(editor);
        check(hosted != nullptr);

        if (hosted == nullptr)
            return;

        check(hosted->isGuiCreated() && !hosted->isGuiAttached());
        check(sameSize(hosted->initialSize(), HostFixtures::initialSize));

        // Fixed-size: the user may not drag it, and it accepts only its own size.
        check(!hosted->constraints().resizable);
        check(
            sameSize(hosted->constrainSize({600, 500}), HostFixtures::initialSize));
    }

    {
        auto loaded = loadFixture(formats, "HostFixtureEffect", hostedEditors());
        check(loaded.plugin != nullptr);

        if (loaded.plugin == nullptr)
            return;

        auto editor = loaded.plugin->createEditor();
        auto* hosted = asCLAP(editor);
        check(hosted != nullptr);

        if (hosted == nullptr)
            return;

        // GenericEditor, resizable within its bounds, which adjust_size applies.
        auto size = hosted->initialSize();
        check(size.width > 0 && size.height > 0);
        check(hosted->constraints().resizable);

        auto smallest = hosted->constrainSize({10, 10});
        check(smallest.width > 10 && smallest.height > 10);
    }
};

auto tNoFactory = test("HostedCLAPEditor/aHostWithoutEditorsShowsNone") = []
{
    const auto formats = DefaultFormats();
    auto plain = loadFixture(formats, "HostFixtureEffect", {});
    check(plain.plugin != nullptr && plain.plugin->hasEditor());
    check(plain.plugin != nullptr && plain.plugin->createEditor() == nullptr);

    auto shown = loadFixture(formats, "HostFixtureEffect", hostedEditors());
    check(shown.plugin != nullptr && shown.plugin->createEditor() != nullptr);
};

auto tOneAtATime = test("HostedCLAPEditor/oneGuiPerInstanceAtATime") = []
{
    auto stub = CLAPGuiStub::Plugin();
    auto plugin = CLAPGuiStub::load(stub, hostedEditors());

    if (plugin == nullptr)
        return;

    auto first = plugin->createEditor();
    check(asCLAP(first) != nullptr);

    // Never asked to create a second GUI while the first lives.
    check(plugin->createEditor() == nullptr);
    check(stub.count("create") == 1);

    first.reset();
    auto again = plugin->createEditor();
    check(asCLAP(again) != nullptr);
    check(stub.count("create") == 2);

    again.reset();
    plugin.reset();
    check(CLAPGuiStub::isClean(stub));
};

auto tCreateFails = test("HostedCLAPEditor/aGuiThePluginWontCreateIsNoEditor") = []
{
    auto stub = CLAPGuiStub::Plugin();
    stub.createSucceeds = false;
    auto plugin = CLAPGuiStub::load(stub, hostedEditors());

    if (plugin == nullptr)
        return;

    check(plugin->createEditor() == nullptr);
    check(stub.count("destroy") == 0);

    // Not listening: the plugin's request is refused.
    check(!stub.requestResize(400, 300));

    plugin.reset();
    check(CLAPGuiStub::isClean(stub));
};

// is_api_supported and create in the platform's API, set_scale only where it
// speaks pixels, then get_size; and destroy alone for a GUI never shown.
auto tOrder =
    test("HostedCLAPEditor/theGuiIsCreatedAndDestroyedInTheSpecsOrder") = []
{
    auto stub = CLAPGuiStub::Plugin();
    auto plugin = CLAPGuiStub::load(stub, hostedEditors());

    if (plugin == nullptr)
        return;

    auto editor = plugin->createEditor();
    check(asCLAP(editor) != nullptr);

    auto expected = std::vector<std::string> {"is_api_supported", "create"};

    if (CLAPEditorPlatform::usesScale())
        expected.push_back("set_scale");

    expected.push_back("get_size");
    check(stub.callsSince(0) == expected);
    check(stub.api == CLAPEditorPlatform::windowApi());
    check(stub.scale > 0.0 || !CLAPEditorPlatform::usesScale());

    auto units = Units(stub);
    check(sameSize(editor->initialSize(), units.size(300, 200)));

    auto mark = stub.calls.size();
    editor.reset();
    check(stub.callsSince(mark) == std::vector<std::string> {"destroy"});

    plugin.reset();
    check(stub.calls.back() == "plugin destroy");
    check(CLAPGuiStub::isClean(stub));
};

auto tHints = test("HostedCLAPEditor/theConstraintsComeFromTheResizeHints") = []
{
    auto stub = CLAPGuiStub::Plugin();
    auto plugin = CLAPGuiStub::load(stub, hostedEditors());

    if (plugin == nullptr)
        return;

    auto editor = plugin->createEditor();
    auto* hosted = asCLAP(editor);
    check(hosted != nullptr);

    if (hosted == nullptr)
        return;

    auto units = Units(stub);
    auto initial = hosted->initialSize();

    // A ratio to keep.
    stub.hints = {true, true, true, 16, 9};
    auto limits = hosted->constraints();
    check(limits.resizable);
    check(limits.aspectRatio.width == 16 && limits.aspectRatio.height == 9);

    // Both ways free: adjust_size rounds down to its steps.
    stub.hints = {true, true, false, 0, 0};
    check(hosted->constraints().aspectRatio.width == 0);

    auto adjusted = hosted->constrainSize({initial.width + 57, initial.height + 83});
    auto adjust = [&](int points)
    { return units.points(stub.adjust(units.units(points))); };
    check(sameSize(adjusted,
                   {adjust(initial.width + 57), adjust(initial.height + 83)}));

    // Only horizontally: the height stays where it is.
    stub.hints = {true, false, false, 0, 0};
    check(hosted->constraints().resizable);

    auto wider = hosted->constrainSize({initial.width + 57, initial.height + 83});
    check(wider.width == adjust(initial.width + 57));
    check(wider.height == adjust(initial.height));

    // Neither way, or no can_resize: fixed at its size.
    stub.hints = {false, false, false, 0, 0};
    check(!hosted->constraints().resizable);

    stub.hints = {true, true, false, 0, 0};
    stub.resizable = false;
    check(!hosted->constraints().resizable);
    check(sameSize(hosted->constrainSize({900, 900}), initial));

    // No hints at all: can_resize alone decides.
    stub.resizable = true;
    stub.hasHints = false;
    check(hosted->constraints().resizable);
    check(hosted->constraints().aspectRatio.width == 0);

    editor.reset();
    plugin.reset();
    check(CLAPGuiStub::isClean(stub));
};

auto tHintsChanged = test("HostedCLAPEditor/resizeHintsChangedReachesTheEditor") = []
{
    auto stub = CLAPGuiStub::Plugin();
    auto plugin = CLAPGuiStub::load(stub, hostedEditors());

    if (plugin == nullptr)
        return;

    auto editor = plugin->createEditor();
    auto* hosted = asCLAP(editor);
    check(hosted != nullptr);

    if (hosted == nullptr)
        return;

    auto changes = 0;
    hosted->onConstraintsChanged = [&] { ++changes; };

    stub.hintsChanged();
    check(changes == 1);

    // From another thread: kept for the main thread's next service.
    stub.fromAnotherThread(
        [&]
        {
            stub.hintsChanged();
            return 0;
        });
    check(changes == 1);

    plugin->syncEditorWithProcessor();
    check(changes == 2);

    editor.reset();
    plugin.reset();
    check(CLAPGuiStub::isClean(stub));
};

// request_resize is the editor's own resize: to the host on the main thread at
// once, from another thread acknowledged and delivered by the next service, and
// a deferred one the host refuses undone through set_size.
auto tOwnResize = test("HostedCLAPEditor/aRequestResizeReachesTheEditorsHost") = []
{
    auto stub = CLAPGuiStub::Plugin();
    auto plugin = CLAPGuiStub::load(stub, hostedEditors());

    if (plugin == nullptr)
        return;

    auto editor = plugin->createEditor();
    auto* hosted = asCLAP(editor);
    check(hosted != nullptr);

    if (hosted == nullptr)
        return;

    auto units = Units(stub);

    // No host yet: nobody to take it.
    check(!stub.requestResize(400, 300));

    auto host = RecordingHost();
    hosted->view();
    hosted->setHost(&host);

    check(stub.requestResize(400, 300));
    check(host.requests.size() == 1
          && sameSize(host.requests.back(), units.size(400, 300)));

    auto sent = stub.fromAnotherThread([&] { return stub.requestResize(500, 350); });
    check(sent);
    check(host.requests.size() == 1);

    plugin->syncEditorWithProcessor();
    check(host.requests.size() == 2
          && sameSize(host.requests.back(), units.size(500, 350)));

    // Delivered once.
    plugin->syncEditorWithProcessor();
    check(host.requests.size() == 2);

    // Only the latest of several is delivered.
    stub.fromAnotherThread(
        [&]
        {
            stub.requestResize(510, 360);
            return stub.requestResize(520, 370);
        });
    plugin->syncEditorWithProcessor();
    check(host.requests.size() == 3
          && sameSize(host.requests.back(), units.size(520, 370)));

    // Refused on the main thread: the plugin hears no.
    host.grant = false;
    auto setBefore = stub.sizesSet.size();
    check(!stub.requestResize(600, 400));
    check(stub.sizesSet.size() == setBefore);

    // Refused after the plugin was told yes: set back to the size it had.
    check(stub.fromAnotherThread([&] { return stub.requestResize(610, 410); }));
    plugin->syncEditorWithProcessor();
    check(stub.sizesSet.size() == setBefore + 1);

    if (stub.sizesSet.size() == setBefore + 1)
    {
        auto had = hosted->view().getLocalBounds();
        check(stub.sizesSet.back().is(units.units(static_cast<int>(had.w)),
                                      units.units(static_cast<int>(had.h))));
    }

    hosted->setHost(nullptr);
    editor.reset();
    plugin.reset();
    check(CLAPGuiStub::isClean(stub));
};

// Not parented, a show or hide has nothing to act on.
auto tVisibility = test("HostedCLAPEditor/showAndHideNeedAnAttachedGui") = []
{
    auto stub = CLAPGuiStub::Plugin();
    auto plugin = CLAPGuiStub::load(stub, hostedEditors());

    if (plugin == nullptr)
        return;

    auto editor = plugin->createEditor();
    check(asCLAP(editor) != nullptr);
    check(!stub.requestShow());
    check(!stub.requestHide());
    check(stub.count("show") == 0 && stub.count("hide") == 0);

    editor.reset();
    plugin.reset();
    check(CLAPGuiStub::isClean(stub));
};

// closed without was_destroyed leaves the GUI; with it, the editor calls it no
// more and destroys it after the next service, never inside the plugin's own
// call (here its timer, say), whether it came from the main thread or another.
auto tClosed = test("HostedCLAPEditor/aGuiThePluginLostIsDestroyedAndLeftAlone") = []
{
    auto stub = CLAPGuiStub::Plugin();
    auto plugin = CLAPGuiStub::load(stub, hostedEditors());

    if (plugin == nullptr)
        return;

    {
        auto editor = plugin->createEditor();
        auto* hosted = asCLAP(editor);
        check(hosted != nullptr);

        if (hosted == nullptr)
            return;

        stub.closed(false);
        check(hosted->isGuiCreated() && stub.count("destroy") == 0);

        stub.runInside([&] { stub.closed(true); });
        check(!hosted->isGuiCreated() && stub.count("destroy") == 0);

        auto mark = stub.calls.size();
        hosted->initialSize();
        hosted->constraints();
        hosted->constrainSize({500, 400});
        check(!stub.requestResize(400, 300));
        check(!stub.requestShow());
        check(stub.calls.size() == mark);

        plugin->syncEditorWithProcessor();
        check(stub.count("destroy") == 1);
        check(stub.callsSince(mark) == std::vector<std::string> {"destroy"});

        hosted->onRemoved();
        check(stub.calls.size() == mark + 1);
    }

    check(stub.count("destroy") == 1);

    {
        auto editor = plugin->createEditor();
        auto* hosted = asCLAP(editor);
        check(hosted != nullptr);

        if (hosted == nullptr)
            return;

        stub.fromAnotherThread(
            [&]
            {
                stub.closed(true);
                return 0;
            });
        check(hosted->isGuiCreated() && stub.count("destroy") == 1);

        plugin->syncEditorWithProcessor();
        check(!hosted->isGuiCreated() && stub.count("destroy") == 2);
    }

    check(stub.count("destroy") == 2);
    plugin.reset();
    check(CLAPGuiStub::isClean(stub));
};

// The plugin goes first: its GUI is destroyed before it, and the editor left
// behind reaches neither.
auto tPluginFirst =
    test("HostedCLAPEditor/aPluginDestroyedFirstTakesItsGuiFirst") = []
{
    auto stub = CLAPGuiStub::Plugin();
    auto plugin = CLAPGuiStub::load(stub, hostedEditors());

    if (plugin == nullptr)
        return;

    auto editor = plugin->createEditor();
    auto* hosted = asCLAP(editor);
    check(hosted != nullptr);

    if (hosted == nullptr)
        return;

    auto size = hosted->initialSize();
    plugin.reset();

    auto count = stub.calls.size();
    check(count >= 2 && stub.calls[count - 2] == "destroy"
          && stub.calls[count - 1] == "plugin destroy");
    check(!hosted->isGuiCreated());

    check(sameSize(hosted->initialSize(), size));
    check(!hosted->constraints().resizable);
    hosted->onAttached();
    hosted->onRemoved();
    editor.reset();

    check(stub.calls.size() == count);
    check(CLAPGuiStub::isClean(stub));
};

// A plugin loaded with no editors has nobody listening: every request is no.
auto tNoListener =
    test("HostedCLAPEditor/withoutAnEditorTheHostRefusesTheGuisRequests") = []
{
    auto stub = CLAPGuiStub::Plugin();
    auto plugin = CLAPGuiStub::load(stub, {});

    if (plugin == nullptr)
        return;

    check(plugin->createEditor() == nullptr);
    check(!stub.requestResize(400, 300));
    check(!stub.requestShow());
    check(!stub.requestHide());
    check(!stub.fromAnotherThread([&] { return stub.requestResize(400, 300); }));
    stub.hintsChanged();
    stub.closed(true);
    plugin->syncEditorWithProcessor();

    plugin.reset();
    check(stub.count("create") == 0);
    check(CLAPGuiStub::isClean(stub));
};

// What the plugin asked of one GUI from another thread never reaches the next:
// destroying a GUI drops it, and with no GUI nothing is kept.
auto tStale = test("HostedCLAPEditor/aRequestForOneGuiNeverReachesTheNext") = []
{
    auto stub = CLAPGuiStub::Plugin();
    auto plugin = CLAPGuiStub::load(stub, hostedEditors());

    if (plugin == nullptr)
        return;

    auto send = [&]
    {
        return stub.fromAnotherThread(
            [&]
            {
                auto taken = stub.requestResize(500, 350);
                stub.hintsChanged();
                stub.closed(true);
                return taken;
            });
    };

    auto first = plugin->createEditor();
    check(asCLAP(first) != nullptr);
    check(send());

    // Removed and created anew within one service tick.
    first.reset();
    stub.lost = false;

    auto second = plugin->createEditor();
    auto* hosted = asCLAP(second);
    check(hosted != nullptr);

    if (hosted == nullptr)
        return;

    auto host = RecordingHost();
    auto changes = 0;
    hosted->view();
    hosted->setHost(&host);
    hosted->onConstraintsChanged = [&] { ++changes; };

    plugin->syncEditorWithProcessor();
    check(hosted->isGuiCreated());
    check(host.requests.empty() && changes == 0);
    check(stub.count("destroy") == 1);

    // No GUI at all: refused, and nothing kept for the one created after.
    hosted->setHost(nullptr);
    second.reset();
    check(!send());
    stub.lost = false;

    auto third = plugin->createEditor();
    check(asCLAP(third) != nullptr);
    plugin->syncEditorWithProcessor();
    check(third != nullptr && asCLAP(third)->isGuiCreated());
    check(stub.count("destroy") == 2);

    third.reset();
    plugin.reset();
    check(CLAPGuiStub::isClean(stub));
};

// A removed editor has destroyed its GUI and holds it no more: another editor
// of the plugin may create one, and the removed one, destroyed later, calls
// nothing.
auto tRemovedReleases =
    test("HostedCLAPEditor/aRemovedEditorLeavesRoomForAnother") = []
{
    auto stub = CLAPGuiStub::Plugin();
    auto plugin = CLAPGuiStub::load(stub, hostedEditors());

    if (plugin == nullptr)
        return;

    auto first = plugin->createEditor();
    auto* removed = asCLAP(first);
    check(removed != nullptr);

    if (removed == nullptr)
        return;

    removed->onRemoved();
    check(!removed->isGuiCreated() && !plugin->hasGuiListener());

    auto second = plugin->createEditor();
    check(asCLAP(second) != nullptr);
    check(stub.count("create") == 2 && plugin->hasGuiListener());

    // Still the second's: the first going takes nothing with it.
    auto mark = stub.calls.size();
    first.reset();
    check(stub.calls.size() == mark && plugin->hasGuiListener());

    second.reset();
    check(!plugin->hasGuiListener());
    plugin.reset();
    check(CLAPGuiStub::isClean(stub));
};

// The editor listens before set_scale, so what the plugin asks from inside it
// reaches the editor. Only where the platform sends set_scale.
auto tListensBeforeScale =
    test("HostedCLAPEditor/theEditorListensBeforeItSetsTheScale") = []
{
    if (!CLAPEditorPlatform::usesScale())
        return;

    auto stub = CLAPGuiStub::Plugin();
    auto plugin = CLAPGuiStub::load(stub, hostedEditors());

    if (plugin == nullptr)
        return;

    auto heard = false;
    stub.duringSetScale = [&] { heard = plugin->hasGuiListener(); };

    auto editor = plugin->createEditor();
    check(asCLAP(editor) != nullptr);
    check(stub.count("set_scale") >= 1 && heard);

    stub.duringSetScale = {};
    editor.reset();
    plugin.reset();
    check(CLAPGuiStub::isClean(stub));
};
} // namespace
} // namespace HostedCLAPEditorTests
