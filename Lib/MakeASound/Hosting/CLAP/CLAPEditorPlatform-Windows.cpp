#include "HostedCLAPEditor.h"

#include <eacp/Graphics/Window/NativeChildSurface.h>

#include <windows.h>

namespace MakeASound::Hosting::CLAPEditorPlatform
{

const char* windowApi() noexcept
{
    return CLAP_WINDOW_API_WIN32;
}

clap_window windowFor(void* nativeParent) noexcept
{
    auto window = clap_window {};
    window.api = CLAP_WINDOW_API_WIN32;
    window.win32 = nativeParent;
    return window;
}

// win32 sizes are physical pixels, and set_scale is how the plugin hears the
// host's scale.
bool usesScale() noexcept
{
    return true;
}

// The DPI of the child window the GUI is parented into, the system's before
// there is one.
float scaleFor(eacp::Graphics::NativeChildSurface*, void* nativeParent) noexcept
{
    auto dpi = nativeParent != nullptr
                   ? GetDpiForWindow(static_cast<HWND>(nativeParent))
                   : 0u;

    if (dpi == 0)
        dpi = GetDpiForSystem();

    return dpi > 0 ? static_cast<float>(dpi) / 96.f : 1.f;
}

// A window moved to a display of another DPI keeps its size in points: the
// surface reports the new scale (WM_DPICHANGED_AFTERPARENT) with no resize.
void watchScale(eacp::Graphics::NativeChildSurface& surface,
                std::function<void()> onChange)
{
    surface.onBackingScaleChanged = [onChange = std::move(onChange)](float)
    { onChange(); };
}

} // namespace MakeASound::Hosting::CLAPEditorPlatform
