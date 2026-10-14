#include "../../Plugin/VST3/VST3Common.h"
#include "HostedVST3Editor.h"

#include <pluginterfaces/gui/iplugviewcontentscalesupport.h>

#include <windows.h>

namespace MakeASound::Hosting::VST3EditorPlatform
{

Steinberg::FIDString viewType() noexcept
{
    return Steinberg::kPlatformTypeHWND;
}

// ViewRects are physical pixels on Windows: the DPI of the child window the
// plugin is parented into, the system's before there is one.
float scaleFor(void* nativeParent) noexcept
{
    auto dpi = nativeParent != nullptr
                   ? GetDpiForWindow(static_cast<HWND>(nativeParent))
                   : 0u;

    if (dpi == 0)
        dpi = GetDpiForSystem();

    return dpi > 0 ? static_cast<float>(dpi) / 96.f : 1.f;
}

void announceBeforeAttach(Steinberg::IPlugView&) noexcept
{
}

// The only way a plugin learns the scale on Windows.
void announceScale(Steinberg::IPlugView& view, float scale) noexcept
{
    if (auto support =
            Steinberg::FUnknownPtr<Steinberg::IPlugViewContentScaleSupport>(&view))
        support->setContentScaleFactor(scale);
}

} // namespace MakeASound::Hosting::VST3EditorPlatform
