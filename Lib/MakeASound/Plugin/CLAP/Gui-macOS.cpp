#include "GuiView.h"

namespace MakeASound::CLAP
{

const char* GuiView::nativeApi() noexcept
{
    return CLAP_WINDOW_API_COCOA;
}

void* GuiView::nativeParentOf(const clap_window& window) noexcept
{
    return window.cocoa;
}

// Sizes are logical points and AppKit scales the backing store itself, so the
// host's scale is declined, as the spec asks of an API in logical pixels.
bool GuiView::setScale(double) noexcept
{
    return false;
}

} // namespace MakeASound::CLAP
