#include "HostedCLAPEditor.h"

#include <eacp/Graphics/Window/NativeChildSurface.h>

#include <cstdint>

namespace MakeASound::Hosting::CLAPEditorPlatform
{

const char* windowApi() noexcept
{
    return CLAP_WINDOW_API_X11;
}

// The surface's handle is the X11 window id widened into the pointer.
clap_window windowFor(void* nativeParent) noexcept
{
    auto window = clap_window {};
    window.api = CLAP_WINDOW_API_X11;
    window.x11 = static_cast<clap_xwnd>(reinterpret_cast<uintptr_t>(nativeParent));
    return window;
}

// x11 sizes are physical pixels, and X11 has no per-window scale a plugin could
// read: set_scale is the only way it learns one.
bool usesScale() noexcept
{
    return true;
}

// The X11 window's scale (Xft.dpi over 96 for a toplevel); 1 before the surface
// is in a window.
float scaleFor(eacp::Graphics::NativeChildSurface* surface, void*) noexcept
{
    return surface != nullptr ? surface->backingScale() : 1.f;
}

// A display's DPI changes without resizing anything.
void watchScale(eacp::Graphics::NativeChildSurface& surface,
                std::function<void()> onChange)
{
    surface.onBackingScaleChanged = [onChange = std::move(onChange)](float)
    { onChange(); };
}

} // namespace MakeASound::Hosting::CLAPEditorPlatform
