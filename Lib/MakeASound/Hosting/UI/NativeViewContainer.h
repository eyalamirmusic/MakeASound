#pragma once

// Internal to the hosted editors: the eacp view a plugin's native view is
// parented into, which reports its own resizes.

#include <eacp/Graphics/Window/NativeChildSurface.h>

#include <functional>

namespace MakeASound::Hosting
{

class NativeViewContainer : public eacp::Graphics::NativeChildSurface
{
public:
    // After the native child was placed over the new bounds.
    std::function<void(int width, int height)> onResized;

    void resized() override
    {
        eacp::Graphics::NativeChildSurface::resized();

        if (!onResized)
            return;

        auto bounds = getLocalBounds();
        onResized(static_cast<int>(bounds.w), static_cast<int>(bounds.h));
    }
};

} // namespace MakeASound::Hosting
