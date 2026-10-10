#include "VST3Common.h"
#include "PlugView.h"

#include <eacp/Graphics/Window/EmbeddedView.h>

namespace MakeASound::VST3
{

FIDString PlugView::nativeViewType() noexcept
{
    return Steinberg::kPlatformTypeX11EmbedWindowID;
}

float PlugView::rectScale() const noexcept
{
    return scale;
}

void PlugView::applyContentScale() noexcept
{
    if (embedded)
        embedded->setPixelsPerPoint(scale);
}

} // namespace MakeASound::VST3
