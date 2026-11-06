#include "VST3Common.h"
#include "PlugView.h"

namespace MakeASound::VST3
{

FIDString PlugView::nativeViewType() noexcept
{
    return Steinberg::kPlatformTypeNSView;
}

// The host speaks points, and AppKit scales the backing store itself.
float PlugView::rectScale() const noexcept
{
    return 1.f;
}

void PlugView::applyContentScale() noexcept
{
}

// The responder chain already goes on from the embedded view into the host's.
bool PlugView::unhandledKey(const eacp::Graphics::KeyEvent&)
{
    return false;
}

} // namespace MakeASound::VST3
