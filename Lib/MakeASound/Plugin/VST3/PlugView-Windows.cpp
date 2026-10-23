#include "VST3Common.h"
#include "PlugView.h"

#include <eacp/Core/Utils/WinInclude.h>
#include <eacp/Graphics/Graphics/Keyboard.h>
#include <eacp/Graphics/Window/EmbeddedView.h>

namespace eacp::Graphics
{
// Keyboard-Windows.cpp's inverse of keyCodeFromVirtualKey, unexported.
int virtualKeyFromKeyCode(uint16_t keyCode);
} // namespace eacp::Graphics

namespace MakeASound::VST3
{

namespace
{
// Physical pixels per point at `window`, or at the system's DPI before there is
// one: the same answer eacp's embedded view reaches on its own when nothing
// overrides it, so a ViewRect and the content agree.
float dpiScaleOf(void* window) noexcept
{
    auto dpi = window != nullptr ? GetDpiForWindow(static_cast<HWND>(window)) : 0u;

    if (dpi == 0)
        dpi = GetDpiForSystem();

    return dpi > 0 ? static_cast<float>(dpi) / 96.f : 1.f;
}
} // namespace

FIDString PlugView::nativeViewType() noexcept
{
    return Steinberg::kPlatformTypeHWND;
}

// A ViewRect is in physical pixels here. The host's content scale says how many
// per point; a host that never calls setContentScaleFactor leaves the parent
// window's DPI, or a 150% display would get a rect of points and content of
// pixels, scaled once too few.
float PlugView::rectScale() const noexcept
{
    return hostScaled ? scale : dpiScaleOf(systemWindow);
}

// 0 has the embedded view follow its window's DPI, the scale rectScale() reads.
void PlugView::applyContentScale() noexcept
{
    if (embedded)
        embedded->setPixelsPerPoint(hostScaled ? scale : 0.f);
}

// Windows has no responder chain, and eacp's fallback re-sends the key to the
// editor's own window, where it goes no further: the DAW would never see its
// shortcuts. So the key is posted to the window the host attached us to, as
// Tamber Plug's VST3 does. Always the plain key message, never WM_SYSKEYDOWN,
// whose DefWindowProc would open the host's window menu; bit 30 of lParam marks
// an auto-repeat down. A key with no virtual key to rebuild it from is dropped,
// and still claimed: declining it would only loop it back into our own window.
bool PlugView::unhandledKey(const eacp::Graphics::KeyEvent& key)
{
    if (systemWindow == nullptr)
        return false;

    auto vk = eacp::Graphics::virtualKeyFromKeyCode(key.keyCode);

    if (vk == 0)
        return true;

    auto isDown = key.type == eacp::Graphics::KeyEventType::Down;

    PostMessageW(static_cast<HWND>(systemWindow),
                 isDown ? UINT {WM_KEYDOWN} : UINT {WM_KEYUP},
                 static_cast<WPARAM>(vk),
                 isDown && key.isRepeat ? LPARAM {0x40000000} : LPARAM {0});
    return true;
}

} // namespace MakeASound::VST3
