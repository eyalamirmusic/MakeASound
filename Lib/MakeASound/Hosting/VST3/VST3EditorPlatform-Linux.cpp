#include "../../Plugin/VST3/VST3Common.h"
#include "HostedVST3Editor.h"

#include <eacp/Graphics/Window/NativeChildSurface.h>

#include <pluginterfaces/gui/iplugviewcontentscalesupport.h>

namespace MakeASound::Hosting::VST3EditorPlatform
{

Steinberg::FIDString viewType() noexcept
{
    return Steinberg::kPlatformTypeX11EmbedWindowID;
}

// ViewRects are pixels on Linux, at the X11 window's scale (Xft.dpi over 96 for
// a toplevel); 1 before the surface is in a window.
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

void announceBeforeAttach(Steinberg::IPlugView&) noexcept
{
}

// As on Windows, the only way a plugin learns the scale.
void announceScale(Steinberg::IPlugView& view, float scale) noexcept
{
    if (auto support =
            Steinberg::FUnknownPtr<Steinberg::IPlugViewContentScaleSupport>(&view))
        support->setContentScaleFactor(scale);
}

} // namespace MakeASound::Hosting::VST3EditorPlatform
