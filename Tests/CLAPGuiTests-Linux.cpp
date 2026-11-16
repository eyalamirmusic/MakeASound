// The Linux half of the CLAP gui: x11, sizes in physical pixels, one per point
// until the host names a scale, and an X11 window of the test's own standing in
// for the DAW's, read back over the test's own xcb connection. The window cases
// need an X server and eacp's X11 backend (EACP_WINDOW_SYSTEM=x11, as eacp's
// Scripts/with-xvfb sets) and skip without one, or fail when
// EACP_REQUIRE_DISPLAY=1. The editor's message loop is the one the host pumps
// through posix-fd-support (CLAPTests' theHostsLoopPumpsTheMessageLoop).

#include "CLAPGuiHost.h"

#include <eacp/Core/App/AppEnvironment.h>
#include <eacp/Core/Utils/Environment.h>

#include <NanoTest/NanoTest.h>

#include <xcb/xcb.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace CLAPGuiTestsLinux
{
namespace
{
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

    nano::check(eacp::getEnvValue("EACP_REQUIRE_DISPLAY") != "1");
    return false;
}
} // namespace
} // namespace CLAPGuiTestsLinux

namespace CLAPGuiHost
{

struct ParentWindow::Native
{
    xcb_connection_t* xcb = nullptr;
    xcb_window_t window = XCB_NONE;

    xcb_query_tree_reply_t* tree() const
    {
        return xcb_query_tree_reply(xcb, xcb_query_tree(xcb, window), nullptr);
    }

    xcb_window_t child() const
    {
        auto* reply = tree();

        if (reply == nullptr)
            return XCB_NONE;

        auto first = xcb_query_tree_children_length(reply) > 0
                         ? xcb_query_tree_children(reply)[0]
                         : XCB_NONE;
        std::free(reply);
        return first;
    }
};

// Never mapped: the plugin's child is still created, sized and mapped in it.
ParentWindow::ParentWindow()
    : native(std::make_unique<Native>())
{
    if (!CLAPGuiTestsLinux::hasDisplay())
        return;

    native->xcb = xcb_connect(nullptr, nullptr);

    if (xcb_connection_has_error(native->xcb) != 0)
        return;

    auto* screen = xcb_setup_roots_iterator(xcb_get_setup(native->xcb)).data;
    native->window = xcb_generate_id(native->xcb);
    xcb_create_window(native->xcb,
                      XCB_COPY_FROM_PARENT,
                      native->window,
                      screen->root,
                      0,
                      0,
                      400,
                      300,
                      0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT,
                      screen->root_visual,
                      0,
                      nullptr);
    xcb_flush(native->xcb);
}

ParentWindow::~ParentWindow()
{
    if (native->xcb == nullptr)
        return;

    if (native->window != XCB_NONE)
        xcb_destroy_window(native->xcb, native->window);

    xcb_disconnect(native->xcb);
}

bool ParentWindow::isValid() const
{
    return native->window != XCB_NONE;
}

clap_window ParentWindow::window() const
{
    auto result = clap_window {};
    result.api = CLAP_WINDOW_API_X11;
    result.x11 = native->window;
    return result;
}

int ParentWindow::childCount() const
{
    auto* reply = native->tree();

    if (reply == nullptr)
        return -1;

    auto count = xcb_query_tree_children_length(reply);
    std::free(reply);
    return count;
}

EditorSize ParentWindow::childSize() const
{
    auto child = native->child();

    if (child == XCB_NONE)
        return {0, 0};

    auto* reply = xcb_get_geometry_reply(
        native->xcb, xcb_get_geometry(native->xcb, child), nullptr);

    if (reply == nullptr)
        return {0, 0};

    auto size = EditorSize {reply->width, reply->height};
    std::free(reply);
    return size;
}

// Mapped, though unviewable in a parent nobody mapped.
bool ParentWindow::childVisible() const
{
    auto child = native->child();

    if (child == XCB_NONE)
        return false;

    auto* reply = xcb_get_window_attributes_reply(
        native->xcb, xcb_get_window_attributes(native->xcb, child), nullptr);

    if (reply == nullptr)
        return false;

    auto mapped = reply->map_state != XCB_MAP_STATE_UNMAPPED;
    std::free(reply);
    return mapped;
}

} // namespace CLAPGuiHost

namespace CLAPGuiTestsLinux
{
using namespace nano;
using namespace CLAPGuiHost;

namespace
{
auto tX11 = test("CLAP/aLinuxGuiIsX11InPixelsAtTheHostsScale") = []
{
    auto host = PluginHost<GainPlugin> {};
    auto scoped = ScopedTestEditor {bounded()};
    auto gui = Gui(host);
    check(gui.created);
    check(gui.api != nullptr && std::strcmp(gui.api, CLAP_WINDOW_API_X11) == 0);

    if (!gui.created)
        return;

    // X11 has no scale of its own to read: one pixel per point until named.
    check(gui.size().is(400, 300));

    check(host.gui->set_scale(host.clap, 2.0));
    check(gui.size().is(800, 600));
    check(gui.adjusted(2000, 100).second.is(1600, 200));

    check(!host.gui->set_scale(host.clap, 0.0));
    check(!host.gui->set_scale(host.clap, -1.0));
    check(gui.size().is(800, 600));

    // A size the host set keeps its points across a scale change.
    check(host.gui->set_size(host.clap, 1000, 700));
    check(host.gui->set_scale(host.clap, 1.0));
    check(gui.size().is(500, 350));

    // The plugin's own request goes out in pixels.
    check(host.gui->set_scale(host.clap, 2.0));
    check(scoped.editor->requestResize({600, 400}));
    check(!host.host.resizeRequests.empty()
          && host.host.resizeRequests.back() == std::pair {1200u, 800u});
    check(gui.size().is(1200, 800));
};

// The embedded surface measures what the host does: at a scale of 2, twice the
// editor's points in pixels, in an X11 parent of that size.
auto tScaledEmbed = test("CLAP/aScaledLinuxGuiEmbedsInPixels") = []
{
    auto parent = ParentWindow {};

    if (!parent.isValid())
        return;

    auto host = PluginHost<GainPlugin> {};
    auto scoped = ScopedTestEditor {bounded()};
    auto gui = Gui(host);
    check(host.gui->set_scale(host.clap, 2.0));

    auto window = parent.window();
    check(host.gui->set_parent(host.clap, &window));
    check(ParentWindow::waitFor([&] { return parent.childCount() == 1; }));

    check(host.gui->set_size(host.clap, 1000, 700));
    check(ParentWindow::waitFor(
        [&]
        {
            auto size = parent.childSize();
            return size.width == 1000 && size.height == 700;
        }));
};
} // namespace
} // namespace CLAPGuiTestsLinux
