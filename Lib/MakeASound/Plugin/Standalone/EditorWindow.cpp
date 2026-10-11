#include "EditorWindow.h"

#include <cmath>

namespace MakeASound::Standalone
{
namespace Graphics = eacp::Graphics;

namespace
{
EditorSize toEditorSize(Graphics::Point point) noexcept
{
    return {static_cast<int>(std::lround(point.x)),
            static_cast<int>(std::lround(point.y))};
}

Graphics::Point toPoint(EditorSize size) noexcept
{
    return {static_cast<float>(size.width), static_cast<float>(size.height)};
}
} // namespace

Graphics::WindowOptions editorWindowOptions(const Editor& editor, std::string title)
{
    auto constraints = editor.constraints();
    auto size = constraints.constrain(editor.initialSize());

    auto options = Graphics::WindowOptions {};
    options.title = std::move(title);
    options.width = size.width;
    options.height = size.height;
    options.minWidth = constraints.minimum.width;
    options.minHeight = constraints.minimum.height;

    options.sizeConstraint = [constraints](const Graphics::ResizeRequest& request)
    { return toPoint(constraints.constrain(toEditorSize(request.size))); };

    if (constraints.aspectRatio.width > 0 && constraints.aspectRatio.height > 0)
        options.allowsFullScreen = false;

    if (!constraints.resizable)
        removeResizable(options);

    return options;
}

void removeResizable(Graphics::WindowOptions& options)
{
    options.flags.eraseIf([](Graphics::WindowFlags flag)
                          { return flag == Graphics::WindowFlags::Resizable; });
}

} // namespace MakeASound::Standalone
