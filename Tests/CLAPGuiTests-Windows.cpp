// The Windows half of the CLAP gui: win32, sizes in physical pixels, the window's
// DPI until the host names a scale and the host's from then on, a hidden popup
// standing in for the DAW's window, and a key the editor left posted to it.

#include "CLAPGuiHost.h"

#include <MakeASound/Plugin/UI/GenericEditor.h>

#include <NanoTest/NanoTest.h>

#include <eacp/Core/Utils/WinInclude.h>
#include <eacp/Graphics/Graphics/Keyboard.h>

#include <cmath>
#include <cstring>

namespace CLAPGuiHost
{

struct ParentWindow::Native
{
    HWND hwnd = nullptr;

    HWND child() const
    {
        return hwnd != nullptr ? GetWindow(hwnd, GW_CHILD) : nullptr;
    }
};

// A popup's client area is the whole window, so the child fills it exactly.
ParentWindow::ParentWindow()
    : native(std::make_unique<Native>())
{
    native->hwnd = CreateWindowExW(0,
                                   L"STATIC",
                                   L"MakeASound CLAP gui parent",
                                   WS_POPUP,
                                   0,
                                   0,
                                   400,
                                   300,
                                   nullptr,
                                   nullptr,
                                   GetModuleHandleW(nullptr),
                                   nullptr);
}

ParentWindow::~ParentWindow()
{
    if (native->hwnd != nullptr)
        DestroyWindow(native->hwnd);
}

bool ParentWindow::isValid() const
{
    return native->hwnd != nullptr;
}

clap_window ParentWindow::window() const
{
    auto result = clap_window {};
    result.api = CLAP_WINDOW_API_WIN32;
    result.win32 = native->hwnd;
    return result;
}

int ParentWindow::childCount() const
{
    auto count = 0;

    for (auto* child = native->child(); child != nullptr;
         child = GetWindow(child, GW_HWNDNEXT))
        ++count;

    return count;
}

EditorSize ParentWindow::childSize() const
{
    auto* child = native->child();
    auto rect = RECT {};

    if (child == nullptr || !GetWindowRect(child, &rect))
        return {0, 0};

    return {static_cast<int>(rect.right - rect.left),
            static_cast<int>(rect.bottom - rect.top)};
}

// The window's own flag: the parent is never shown, so IsWindowVisible would
// answer no for any child.
bool ParentWindow::childVisible() const
{
    auto* child = native->child();
    return child != nullptr && (GetWindowLongW(child, GWL_STYLE) & WS_VISIBLE) != 0;
}

} // namespace CLAPGuiHost

namespace CLAPGuiTestsWindows
{
using namespace nano;
using namespace CLAPGuiHost;

namespace
{
int scaled(int points, float factor)
{
    return static_cast<int>(std::lround(static_cast<float>(points) * factor));
}

auto tWin32 = test("CLAP/aWindowsGuiIsWin32InPixelsAtTheHostsScale") = []
{
    auto host = PluginHost<GainPlugin> {};
    auto scoped = ScopedTestEditor {bounded()};
    auto gui = Gui(host);
    check(gui.created);
    check(gui.api != nullptr && std::strcmp(gui.api, CLAP_WINDOW_API_WIN32) == 0);

    if (!gui.created)
        return;

    // Not yet parented, and no scale named: the system's DPI, as eacp's guess.
    auto dpi = static_cast<float>(GetDpiForSystem()) / 96.f;
    check(gui.size().is(scaled(400, dpi), scaled(300, dpi)));

    // Once the host names a scale it wins over the DPI.
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
// editor's points in pixels.
auto tScaledEmbed = test("CLAP/aScaledWindowsGuiEmbedsInPixels") = []
{
    auto parent = ParentWindow {};
    check(parent.isValid());

    auto host = PluginHost<GainPlugin> {};
    auto scoped = ScopedTestEditor {bounded()};
    auto gui = Gui(host);
    check(host.gui->set_scale(host.clap, 2.0));

    auto window = parent.window();
    check(host.gui->set_parent(host.clap, &window));
    check(ParentWindow::waitFor(
        [&]
        {
            auto size = parent.childSize();
            return size.width == 800 && size.height == 600;
        }));
};

// The DAW's shortcuts: a key the editor did not consume is posted to the window
// the host gave set_parent, as the VST3 view does.
auto tKeyPosted = test("CLAP/anUnhandledKeyIsPostedToTheHostsWindow") = []
{
    auto parent = ParentWindow {};
    check(parent.isValid());

    auto host = PluginHost<GainPlugin> {};
    auto scoped = ScopedTestEditor {bounded()};
    auto gui = Gui(host);

    auto key = eacp::Graphics::KeyEvent {};
    key.keyCode = eacp::Graphics::KeyCode::Space;
    key.type = eacp::Graphics::KeyEventType::Down;

    // Not yet parented: no window to post to.
    check(!scoped.editor->forwardUnhandledKey(key));

    auto window = parent.window();
    check(host.gui->set_parent(host.clap, &window));
    check(scoped.editor->forwardUnhandledKey(key));

    auto message = MSG {};
    auto hwnd = static_cast<HWND>(parent.window().win32);
    check(PeekMessageW(&message, hwnd, WM_KEYFIRST, WM_KEYLAST, PM_REMOVE));
    check(message.message == WM_KEYDOWN);
    check(message.wParam == VK_SPACE);
};
} // namespace
} // namespace CLAPGuiTestsWindows
