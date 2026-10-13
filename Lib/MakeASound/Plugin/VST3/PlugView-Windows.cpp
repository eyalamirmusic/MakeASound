#include "VST3Common.h"
#include "PlugView.h"

#include <eacp/Core/Utils/WinInclude.h>
#include <eacp/Graphics/Window/EmbeddedView.h>

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

} // namespace MakeASound::VST3
