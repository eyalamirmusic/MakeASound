// The Windows half of the VST3 view's scaling: a ViewRect is in physical pixels
// there, and a host that never calls setContentScaleFactor leaves the window's
// DPI as the only scale to go by. And its keys: one the editor did not consume is
// posted to the host's window, there being no responder chain to carry it.

#include "VST3TestHost.h"

#include <MakeASound/Plugin/UI/GenericEditor.h>
#include <MakeASound/Plugin/VST3/PlugView.h>

#include <NanoTest/NanoTest.h>

#include <eacp/Core/Utils/WinInclude.h>
#include <eacp/Graphics/Graphics/Keyboard.h>

#include <cmath>

namespace VST3ViewTestsWindows
{
using namespace nano;
using namespace TestPlugins;
using namespace VST3Host;

namespace
{
Steinberg::ViewRect sizeOf(Steinberg::IPlugView& view)
{
    auto rect = Steinberg::ViewRect {};
    check(view.getSize(&rect) == Steinberg::kResultOk);
    return rect;
}

int scaled(int points, float factor)
{
    return static_cast<int>(std::lround(static_cast<float>(points) * factor));
}

auto tUnscaled = test("VST3/anUnscaledWindowsViewFollowsTheDpi") = []
{
    auto host = AdapterHost<GainPlugin> {};
    auto view = Steinberg::owned(host.adapter->createView(Vst::ViewType::kEditor));
    check(view != nullptr);

    auto points = MakeASound::GenericEditor(host.adapter->plugin()).initialSize();
    auto dpi = static_cast<float>(GetDpiForSystem()) / 96.f;

    // Not yet parented: the system's DPI is the guess, as eacp's own.
    auto unscaled = sizeOf(*view);
    check(unscaled.getWidth() == scaled(points.width, dpi));
    check(unscaled.getHeight() == scaled(points.height, dpi));

    // Once the host names a scale, it wins over the DPI.
    auto scaling =
        Steinberg::FUnknownPtr<Steinberg::IPlugViewContentScaleSupport>(view.get());
    check(scaling->setContentScaleFactor(2.f) == Steinberg::kResultOk);

    auto hostScaled = sizeOf(*view);
    check(hostScaled.getWidth() == points.width * 2);
    check(hostScaled.getHeight() == points.height * 2);
};

// A hidden top-level window standing in for the DAW's, destroyed on the way out.
struct ParentWindow
{
    ParentWindow()
        : hwnd(CreateWindowExW(0,
                               L"STATIC",
                               L"MakeASound key parent",
                               WS_OVERLAPPEDWINDOW,
                               0,
                               0,
                               640,
                               480,
                               nullptr,
                               nullptr,
                               GetModuleHandleW(nullptr),
                               nullptr))
    {
    }

    ~ParentWindow()
    {
        if (hwnd != nullptr)
            DestroyWindow(hwnd);
    }

    ParentWindow(const ParentWindow&) = delete;
    ParentWindow& operator=(const ParentWindow&) = delete;

    // The first key message waiting for the window, taken off the queue.
    MSG takeKey() const
    {
        auto message = MSG {};

        if (!PeekMessageW(&message, hwnd, WM_KEYFIRST, WM_KEYLAST, PM_REMOVE))
            message.message = 0;

        return message;
    }

    HWND hwnd;
};

auto tKeyPosted = test("VST3/anUnhandledKeyIsPostedToTheHostsWindow") = []
{
    auto host = AdapterHost<GainPlugin> {};
    auto view = Steinberg::owned(host.adapter->createView(Vst::ViewType::kEditor));
    check(view != nullptr);

    auto parent = ParentWindow {};
    check(parent.hwnd != nullptr);
    check(view->attached(parent.hwnd, Steinberg::kPlatformTypeHWND)
          == Steinberg::kResultOk);

    auto& asHost = *static_cast<MakeASound::VST3::PlugView*>(view.get());

    auto key = eacp::Graphics::KeyEvent {};
    key.keyCode = eacp::Graphics::KeyCode::Space;
    key.type = eacp::Graphics::KeyEventType::Down;
    key.isRepeat = true;
    check(asHost.unhandledKey(key));

    auto down = parent.takeKey();
    check(down.message == WM_KEYDOWN);
    check(down.wParam == VK_SPACE);
    check((down.lParam & 0x40000000) != 0);

    key.type = eacp::Graphics::KeyEventType::Up;
    key.isRepeat = false;
    check(asHost.unhandledKey(key));

    auto up = parent.takeKey();
    check(up.message == WM_KEYUP);
    check(up.wParam == VK_SPACE);

    // Once removed the handle is no longer ours to post to.
    check(view->removed() == Steinberg::kResultOk);
    check(!asHost.unhandledKey(key));
    check(parent.takeKey().message == 0);
};
} // namespace
} // namespace VST3ViewTestsWindows
