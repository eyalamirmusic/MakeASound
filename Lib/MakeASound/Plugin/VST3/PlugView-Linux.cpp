#include "VST3Common.h"
#include "PlugView.h"

namespace MakeASound::VST3
{

// The parent is an X11 window id, which travels in attached()'s pointer.
FIDString PlugView::nativeViewType() noexcept
{
    return Steinberg::kPlatformTypeX11EmbedWindowID;
}

} // namespace MakeASound::VST3
