#include "ViewPlacement.h"

#include <algorithm>
#include <cmath>

namespace MakeASound::AU
{

bool isEditorWindow(const ViewFrame& window, const ViewFrame& view) noexcept
{
    return window.width - view.width <= maxEditorWindowChromeWidth
           && window.height - view.height <= maxEditorWindowChromeHeight;
}

bool isHostPluginContainer(const ViewFrame& parent,
                           bool parentIsContentView,
                           const ViewFrame& view) noexcept
{
    return !parentIsContentView && parent.width == view.width
           && parent.height == view.height;
}

ViewOrigin containedOrigin(const ViewFrame& window,
                           const ViewFrame& visible) noexcept
{
    auto x = std::max(visible.x,
                      std::min(window.x, visible.x + visible.width - window.width));
    auto y = std::min(std::max(window.y, visible.y),
                      visible.y + visible.height - window.height);

    return {x, y};
}

std::optional<EditorSize> shrinkToFit(const ViewFrame& view,
                                      double overhang,
                                      double minimumHeight) noexcept
{
    if (overhang <= 0.0 || view.height <= 0.0)
        return std::nullopt;

    auto height = view.height - overhang;

    if (height < minimumHeight)
        return std::nullopt;

    auto width = view.width * (height / view.height);

    return EditorSize {static_cast<int>(std::lround(width)),
                       static_cast<int>(std::lround(height))};
}

} // namespace MakeASound::AU
