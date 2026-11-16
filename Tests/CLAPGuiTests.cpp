// The CLAP gui hosted in-process through the C API, where the UI is built: the
// extension offered for a plugin with an editor, its window API, create and
// destroy over the plugin's editor or GenericEditor, sizes, resize hints and
// adjust_size from the editor's constraints, set_size, the plugin's own resize
// through the host's request_resize, the gui as the editor's host, the editor's
// gestures reaching the host while it is open, and the editor embedded in a
// native window of the platform's. Each platform's window API and scaling, and
// its window: CLAPGuiTests-<platform>.

#include "CLAPGuiHost.h"

#include <MakeASound/Plugin/UI/GenericEditor.h>

#include <eacp/Core/Threads/EventLoop.h>
#include <eacp/Graphics/View/View.h>

#include <NanoTest/NanoTest.h>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace CLAPGuiTests
{
using namespace nano;
using namespace CLAPGuiHost;

namespace
{
bool near(double a, double b, double tolerance = 1e-4)
{
    return std::abs(a - b) < tolerance;
}

auto tOffered = test("CLAP/aPluginWithAnEditorOffersAnEmbeddedGui") = []
{
    auto host = PluginHost<GainPlugin> {};
    check(host.adapter().plugin().hasEditor());
    check(host.gui != nullptr);

    if (host.gui == nullptr)
        return;

    const char* api = nullptr;
    auto isFloating = true;
    check(host.gui->get_preferred_api(host.clap, &api, &isFloating));
    check(api != nullptr && !isFloating);

    check(host.gui->is_api_supported(host.clap, api, false));
    check(!host.gui->is_api_supported(host.clap, api, true));
    check(!host.gui->is_api_supported(host.clap, CLAP_WINDOW_API_WAYLAND, false));
    check(!host.gui->is_api_supported(host.clap, "other", false));
    check(!host.gui->is_api_supported(host.clap, nullptr, false));

    // Only one of the desktop APIs is this platform's.
    auto supported = 0;

    for (const auto* each:
         {CLAP_WINDOW_API_COCOA, CLAP_WINDOW_API_WIN32, CLAP_WINDOW_API_X11})
        supported += host.gui->is_api_supported(host.clap, each, false) ? 1 : 0;

    check(supported == 1);
};

auto tGeneric = test("CLAP/theGuiIsTheGenericEditor") = []
{
    auto host = PluginHost<GainPlugin> {};
    auto gui = Gui(host);
    check(gui.created);

    if (!gui.created)
        return;

    // One gui at a time, and only an embedded one.
    check(!host.gui->create(host.clap, gui.api, false));

    // Points everywhere at a scale of 1, which macOS declines as it speaks points.
    host.gui->set_scale(host.clap, 1.0);

    auto expected = MakeASound::GenericEditor(host.adapter().plugin()).initialSize();
    check(gui.size().is(expected.width, expected.height));
    check(host.gui->can_resize(host.clap));

    gui.destroy();
    check(!host.gui->create(host.clap, gui.api, true));
    check(!host.gui->create(host.clap, "other", false));
    check(!host.gui->create(host.clap, nullptr, false));
    check(host.gui->create(host.clap, gui.api, false));
    host.gui->destroy(host.clap);
};

// Before create there is no view to answer, and destroy has nothing to drop.
auto tNotCreated = test("CLAP/aGuiNotCreatedAnswersNothing") = []
{
    auto host = PluginHost<GainPlugin> {};
    check(host.gui != nullptr);

    if (host.gui == nullptr)
        return;

    auto width = uint32_t {0};
    auto height = uint32_t {0};
    auto hints = clap_gui_resize_hints {};
    auto window = clap_window {};

    check(!host.gui->get_size(host.clap, &width, &height));
    check(!host.gui->can_resize(host.clap));
    check(!host.gui->get_resize_hints(host.clap, &hints));
    check(!host.gui->adjust_size(host.clap, &width, &height));
    check(!host.gui->set_size(host.clap, 100, 100));
    check(!host.gui->set_parent(host.clap, &window));
    check(!host.gui->show(host.clap));
    check(!host.gui->hide(host.clap));
    check(!host.gui->set_transient(host.clap, &window));
    host.gui->suggest_title(host.clap, "Title");
    host.gui->destroy(host.clap);
};

auto tCanResize = test("CLAP/canResizeFollowsTheEditorsConstraints") = []
{
    auto host = PluginHost<GainPlugin> {};

    {
        auto fixed = ScopedTestEditor {};
        auto gui = Gui(host);
        host.gui->set_scale(host.clap, 1.0);
        check(!host.gui->can_resize(host.clap));
        check(!gui.adjusted(900, 900).first);

        auto hints = gui.hints();
        check(!hints.can_resize_horizontally && !hints.can_resize_vertically);
        check(!hints.preserve_aspect_ratio);
        check(gui.size().is(400, 300));
    }

    auto resizable = ScopedTestEditor {bounded(), {1000, 50}};
    auto gui = Gui(host);
    host.gui->set_scale(host.clap, 1.0);
    check(host.gui->can_resize(host.clap));
    check(gui.size().is(800, 100));
};

auto tHints = test("CLAP/resizeHintsCarryTheEditorsAspectRatio") = []
{
    auto host = PluginHost<GainPlugin> {};

    {
        auto unlocked = ScopedTestEditor {bounded()};
        auto gui = Gui(host);
        auto hints = gui.hints();
        check(hints.can_resize_horizontally && hints.can_resize_vertically);
        check(!hints.preserve_aspect_ratio);
    }

    auto ratio = ScopedTestEditor {ratioLocked(), {600, 400}};
    auto gui = Gui(host);
    auto hints = gui.hints();
    check(hints.can_resize_horizontally && hints.can_resize_vertically);
    check(hints.preserve_aspect_ratio);
    check(hints.aspect_ratio_width == 3 && hints.aspect_ratio_height == 2);
};

auto tAdjust = test("CLAP/adjustSizeAppliesTheEditorsPolicy") = []
{
    auto host = PluginHost<GainPlugin> {};

    for (auto scale: {1.0, 2.0})
    {
        auto bounds = ScopedTestEditor {bounded()};
        auto boundsGui = Gui(host);
        host.gui->set_scale(host.clap, scale);

        // Pixels per point: 1 on macOS whatever the scale.
        auto factor = static_cast<int>(boundsGui.size().width) / 400;
        auto px = [factor](int points)
        { return static_cast<uint32_t>(points * factor); };

        auto large = boundsGui.adjusted(px(1000), px(50));
        check(large.first && large.second.is(px(800), px(100)));

        auto again = boundsGui.adjusted(large.second.width, large.second.height);
        check(again.second.is(px(800), px(100)));
        check(boundsGui.adjusted(px(500), px(400)).second.is(px(500), px(400)));

        // Asking changes nothing.
        check(boundsGui.size().is(px(400), px(300)));
        boundsGui.destroy();

        auto ratio = ScopedTestEditor {ratioLocked(), {600, 400}};
        auto ratioGui = Gui(host);
        host.gui->set_scale(host.clap, scale);

        auto skewed = ratioGui.adjusted(px(600), px(600)).second;
        check(std::abs(static_cast<int>(skewed.width) * 2
                       - static_cast<int>(skewed.height) * 3)
              <= 3 * factor);

        auto skewedAgain = ratioGui.adjusted(skewed.width, skewed.height).second;
        check(skewedAgain.is(skewed.width, skewed.height));
        check(ratioGui.adjusted(px(900), px(600)).second.is(px(900), px(600)));
    }
};

// Whatever the host settles on is the size, as a VST3 host's onSize.
auto tSetSize = test("CLAP/setSizeIsTheSizeTheHostHolds") = []
{
    auto host = PluginHost<GainPlugin> {};
    auto scoped = ScopedTestEditor {bounded()};
    auto gui = Gui(host);

    check(host.gui->set_size(host.clap, 500, 400));
    check(gui.size().is(500, 400));
    check(host.host.resizeRequests.empty());
};

auto tNoHostGui = test("CLAP/requestResizeWithoutTheHostsGuiIsRefused") = []
{
    auto host = PluginHost<GainPlugin> {};
    host.host.offersGui = false;
    auto scoped = ScopedTestEditor {bounded()};
    auto gui = Gui(host);
    host.gui->set_scale(host.clap, 1.0);

    check(!scoped.editor->requestResize({500, 500}));
    check(gui.size().is(400, 300));
};

auto tRequest = test("CLAP/requestResizeGoesThroughTheHostsGui") = []
{
    auto host = PluginHost<GainPlugin> {};
    auto scoped = ScopedTestEditor {bounded()};
    auto gui = Gui(host);
    host.gui->set_scale(host.clap, 1.0);
    auto& requests = host.host.resizeRequests;

    // Accepted, and set_size not called back: the plugin takes the size itself.
    check(scoped.editor->requestResize({1000, 450}));
    check(requests.size() == 1);
    check(requests.size() == 1 && requests.back() == std::pair {800u, 450u});
    check(gui.size().is(800, 450));

    // Accepted with a set_size of the host's own choosing, which is the size.
    host.host.onResizeRequest = [&](uint32_t width, uint32_t height)
    { host.gui->set_size(host.clap, width - 10, height); };
    check(scoped.editor->requestResize({300, 50}));
    check(requests.size() == 2 && requests.back() == std::pair {300u, 100u});
    check(gui.size().is(290, 100));
    host.host.onResizeRequest = nullptr;

    // Refused: the old size stays.
    host.host.acceptResize = false;
    check(!scoped.editor->requestResize({600, 600}));
    check(requests.size() == 3);
    check(gui.size().is(290, 100));

    // The plugin's own size, fixed editor or not, goes the same way.
    host.host.acceptResize = true;
    check(scoped.editor->requestOwnResize({700, 500}));
    check(gui.size().is(700, 500));
    check(host.host.resizeHintsChanges == 0);
};

// Records whether the gui had cleared the host by the time it destroyed us.
struct TeardownEditor final : Editor
{
    explicit TeardownEditor(bool& clearedToSet)
        : cleared(clearedToSet)
    {
    }

    ~TeardownEditor() override { cleared = host() == nullptr; }

    eacp::Graphics::View& view() override { return inner.view(); }

    bool& cleared;
    TestEditor inner;
};

auto tHost = test("CLAP/theGuiIsTheEditorsHostForItsLifetime") = []
{
    auto host = PluginHost<GainPlugin> {};

    {
        auto scoped = ScopedTestEditor {bounded()};
        auto gui = Gui(host);
        check(scoped.editor->host() != nullptr);
        check(scoped.editor->host() != nullptr
              && scoped.editor->host()->drawsResizeHandle());
        check(scoped.editor->attached == 0);
    }

    auto cleared = false;
    Module::editorFactory = [&cleared](Plugin&)
    { return OwningPointer<Editor>(EA::makeOwned<TeardownEditor>(cleared)); };

    {
        auto gui = Gui(host);
        Module::editorFactory = nullptr;
        check(gui.created);
    }

    check(cleared);
};

// A host that destroys the plugin with its gui still up: the adapter drops it
// first, while the plugin it shows is still there.
auto tLeftOpen = test("CLAP/aGuiLeftOpenGoesWithThePlugin") = []
{
    auto cleared = false;
    Module::editorFactory = [&cleared](Plugin&)
    { return OwningPointer<Editor>(EA::makeOwned<TeardownEditor>(cleared)); };

    {
        auto host = PluginHost<GainPlugin> {};
        auto gui = Gui(host);
        Module::editorFactory = nullptr;
        check(gui.created);
        gui.created = false;
    }

    Module::editorFactory = nullptr;
    check(cleared);
};

// The editor's gesture, as GenericEditor makes it, while the gui is open: the
// adapter stays the plugin's listener, and the next block carries the gesture.
auto tGesture = test("CLAP/anEditorGestureReachesTheHostWhileTheGuiIsOpen") = []
{
    auto host = PluginHost<GainPlugin> {};
    auto gui = Gui(host);
    check(gui.created);

    auto& plugin = host.plugin();
    auto& params = plugin.params;
    auto index = plugin.parameters().indexOf(&params.gain);
    auto gainId = host.idOf(params.gain);
    auto* listener = plugin.hostEditListener();
    check(listener == static_cast<HostEditListener*>(&host.adapter()));

    if (listener == nullptr)
        return;

    // The value the editor reports is the parameter's own, as GenericEditor's.
    listener->beginParameterEdit(index);
    params.gain.setNormalized(0.3f);
    listener->performParameterEdit(index, params.gain.getNormalized());
    listener->endParameterEdit(index);

    host.clearEvents();
    host.fillBlock(blockSize);
    host.run();

    // The gain's own events, in order, among whatever else the block carries.
    auto gainEvents = std::vector<const AnyEvent*> {};

    for (const auto& event: host.out.events)
    {
        auto type = event.header.type;
        auto isGesture = type == CLAP_EVENT_PARAM_GESTURE_BEGIN
                         || type == CLAP_EVENT_PARAM_GESTURE_END;

        if ((isGesture && event.gesture.param_id == gainId)
            || (type == CLAP_EVENT_PARAM_VALUE && event.param.param_id == gainId))
            gainEvents.push_back(&event);
    }

    check(gainEvents.size() == 3);

    if (gainEvents.size() == 3)
    {
        check(gainEvents[0]->header.type == CLAP_EVENT_PARAM_GESTURE_BEGIN);
        check(gainEvents[1]->header.type == CLAP_EVENT_PARAM_VALUE);
        check(near(gainEvents[1]->param.value, 0.3));
        check(gainEvents[2]->header.type == CLAP_EVENT_PARAM_GESTURE_END);
    }
};

// Embedded in a window of the platform's: the editor attached under the gui as
// its host, the surface at the host's size and following set_size and the
// plugin's own requests, hidden and shown, and gone with destroy.
auto tEmbedded = test("CLAP/theGuiEmbedsInANativeWindow") = []
{
    auto parent = ParentWindow {};

    if (!parent.isValid())
        return;

    auto host = PluginHost<GainPlugin> {};
    auto scoped = ScopedTestEditor {bounded()};
    auto gui = Gui(host);
    host.gui->set_scale(host.clap, 1.0);

    // A window of another API embeds nothing.
    auto foreign = parent.window();
    foreign.api = CLAP_WINDOW_API_WAYLAND;
    check(!host.gui->set_parent(host.clap, &foreign));
    check(scoped.editor->attached == 0);

    auto window = parent.window();
    check(host.gui->set_parent(host.clap, &window));
    check(host.gui->show(host.clap));
    check(scoped.editor->attached == 1);
    check(scoped.editor->hostAtAttach == scoped.editor->host());
    check(scoped.editor->hostAtAttach != nullptr);

    auto at = [&](int width, int height)
    {
        return ParentWindow::waitFor(
            [&]
            {
                auto size = parent.childSize();
                return size.width == width && size.height == height;
            });
    };

    if (parent.childCount() >= 0)
    {
        check(ParentWindow::waitFor([&] { return parent.childCount() == 1; }));
        check(at(400, 300));

        check(host.gui->set_size(host.clap, 500, 350));
        check(at(500, 350));

        check(scoped.editor->requestResize({600, 450}));
        check(at(600, 450));

        check(host.gui->hide(host.clap));
        check(ParentWindow::waitFor([&] { return !parent.childVisible(); }));
        check(host.gui->show(host.clap));
        check(ParentWindow::waitFor([&] { return parent.childVisible(); }));
    }

    gui.destroy();

    if (parent.childCount() >= 0)
        check(ParentWindow::waitFor([&] { return parent.childCount() == 0; }));
};
} // namespace
} // namespace CLAPGuiTests

void CLAPGuiHost::ParentWindow::pump()
{
    eacp::Threads::runEventLoopUntil([] { return false; }, eacp::Time::MS {20});
}
