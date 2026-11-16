#include "HostedCLAPEditor.h"

namespace MakeASound::Hosting::CLAPEditorPlatform
{

const char* windowApi() noexcept
{
    return CLAP_WINDOW_API_COCOA;
}

clap_window windowFor(void* nativeParent) noexcept
{
    auto window = clap_window {};
    window.api = CLAP_WINDOW_API_COCOA;
    window.cocoa = nativeParent;
    return window;
}

// Cocoa's sizes are logical points: the spec says not to call set_scale, and
// AppKit scales the backing store itself.
bool usesScale() noexcept
{
    return false;
}

float scaleFor(eacp::Graphics::NativeChildSurface*, void*) noexcept
{
    return 1.f;
}

void watchScale(eacp::Graphics::NativeChildSurface&, std::function<void()>)
{
}

} // namespace MakeASound::Hosting::CLAPEditorPlatform
