#include "GuiView.h"

#include <cstdint>

namespace MakeASound::CLAP
{

const char* GuiView::nativeApi() noexcept
{
    return CLAP_WINDOW_API_X11;
}

// The X11 window id widened into the pointer, the currency eacp's embedded view
// takes it in, as the VST3 view receives it from attached().
void* GuiView::nativeParentOf(const clap_window& window) noexcept
{
    return reinterpret_cast<void*>(static_cast<uintptr_t>(window.x11));
}

// Sizes are physical pixels and X11 has no per-window scale to read: the host's,
// or one pixel per point.
bool GuiView::setScale(double scale) noexcept
{
    return applyHostScale(scale);
}

} // namespace MakeASound::CLAP
