#include "GuiView.h"

namespace MakeASound::CLAP
{

const char* GuiView::nativeApi() noexcept
{
    return CLAP_WINDOW_API_WIN32;
}

void* GuiView::nativeParentOf(const clap_window& window) noexcept
{
    return window.win32;
}

// Sizes are physical pixels: the host's scale wins over the parent window's DPI,
// which is what a host that never sets one leaves.
bool GuiView::setScale(double scale) noexcept
{
    return applyHostScale(scale);
}

} // namespace MakeASound::CLAP
