#include "Editor.h"

#include <algorithm>
#include <cmath>

namespace MakeASound
{

namespace
{
int whole(double value) noexcept
{
    return static_cast<int>(std::lround(value));
}
} // namespace

EditorSize EditorConstraints::constrain(EditorSize proposed) const noexcept
{
    auto maxWidth = std::max(maximum.width, minimum.width);
    auto maxHeight = std::max(maximum.height, minimum.height);

    if (aspectRatio.width <= 0 || aspectRatio.height <= 0)
        return {std::clamp(proposed.width, minimum.width, maxWidth),
                std::clamp(proposed.height, minimum.height, maxHeight)};

    auto ratioWidth = static_cast<double>(aspectRatio.width);
    auto ratioHeight = static_cast<double>(aspectRatio.height);

    auto scale = (proposed.width * ratioWidth + proposed.height * ratioHeight)
                 / (ratioWidth * ratioWidth + ratioHeight * ratioHeight);

    auto lowest = std::max(minimum.width / ratioWidth, minimum.height / ratioHeight);
    auto highest = std::min(maxWidth / ratioWidth, maxHeight / ratioHeight);

    scale = std::max(std::min(scale, highest), lowest);

    return {whole(ratioWidth * scale), whole(ratioHeight * scale)};
}

bool Editor::canRequestResize() const
{
    return editorHost != nullptr && constraints().resizable;
}

bool Editor::requestResize(EditorSize size)
{
    if (!canRequestResize())
        return false;

    return editorHost->resizeEditor(constraints().constrain(size));
}

} // namespace MakeASound
