#include "../../Plugin/VST3/VST3Common.h"
#include "HostedVST3Editor.h"

#include <pluginterfaces/gui/iplugviewcontentscalesupport.h>

namespace MakeASound::Hosting::VST3EditorPlatform
{

Steinberg::FIDString viewType() noexcept
{
    return Steinberg::kPlatformTypeNSView;
}

// ViewRects are points on macOS: AppKit scales the backing store itself.
float scaleFor(void*) noexcept
{
    return 1.f;
}

// Some plugins guess a Retina factor and double their own size unless told 1.
void announceBeforeAttach(Steinberg::IPlugView& view) noexcept
{
    if (auto support =
            Steinberg::FUnknownPtr<Steinberg::IPlugViewContentScaleSupport>(&view))
        support->setContentScaleFactor(1.f);
}

void announceScale(Steinberg::IPlugView&, float) noexcept
{
}

} // namespace MakeASound::Hosting::VST3EditorPlatform
