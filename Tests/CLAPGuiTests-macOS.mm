// The macOS half of the CLAP gui: cocoa, sizes in points whatever scale the host
// names, and a plain NSView standing in for the DAW's.

#include "CLAPGuiHost.h"

#include <NanoTest/NanoTest.h>

#import <Cocoa/Cocoa.h>

#include <cstring>

namespace CLAPGuiHost
{

struct ParentWindow::Native
{
    NSView* view = nil;

    NSView* child() const
    {
        return view.subviews.count > 0 ? view.subviews[0] : nil;
    }
};

ParentWindow::ParentWindow()
    : native(std::make_unique<Native>())
{
    native->view = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 400, 300)];
}

ParentWindow::~ParentWindow() = default;

bool ParentWindow::isValid() const
{
    return native->view != nil;
}

clap_window ParentWindow::window() const
{
    auto result = clap_window {};
    result.api = CLAP_WINDOW_API_COCOA;
    result.cocoa = (__bridge void*) native->view;
    return result;
}

int ParentWindow::childCount() const
{
    return static_cast<int>(native->view.subviews.count);
}

EditorSize ParentWindow::childSize() const
{
    auto* child = native->child();

    if (child == nil)
        return {0, 0};

    auto frame = child.frame;
    return {static_cast<int>(frame.size.width), static_cast<int>(frame.size.height)};
}

bool ParentWindow::childVisible() const
{
    auto* child = native->child();
    return child != nil && !child.hidden;
}

} // namespace CLAPGuiHost

namespace CLAPGuiTestsMacOS
{
using namespace nano;
using namespace CLAPGuiHost;

namespace
{
// Cocoa's sizes are logical points: the host's scale is declined, and nothing
// it would have changed moves.
auto tCocoa = test("CLAP/aMacGuiIsCocoaInPointsAndDeclinesTheScale") = []
{
    auto host = PluginHost<GainPlugin> {};
    auto scoped = ScopedTestEditor {bounded()};
    auto gui = Gui(host);
    check(gui.created);
    check(gui.api != nullptr && std::strcmp(gui.api, CLAP_WINDOW_API_COCOA) == 0);

    if (!gui.created)
        return;

    check(gui.size().is(400, 300));
    check(!host.gui->set_scale(host.clap, 2.0));
    check(gui.size().is(400, 300));
    check(gui.adjusted(1000, 50).second.is(800, 100));
};
} // namespace
} // namespace CLAPGuiTestsMacOS
