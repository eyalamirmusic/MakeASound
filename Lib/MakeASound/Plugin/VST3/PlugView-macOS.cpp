#include "VST3Common.h"
#include "PlugView.h"

namespace MakeASound::VST3
{

FIDString PlugView::nativeViewType() noexcept
{
    return Steinberg::kPlatformTypeNSView;
}

} // namespace MakeASound::VST3
