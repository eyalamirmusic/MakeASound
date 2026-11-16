#include "EmbeddedEditor.h"

#include <eacp/Graphics/Window/EmbeddedView.h>

namespace MakeASound
{

// X11 has no per-window scale to read: the host's, or one pixel per point.
float EmbeddedEditor::unitScale() const noexcept
{
    return scale;
}

void EmbeddedEditor::applyScale() noexcept
{
    if (embedded)
        embedded->setPixelsPerPoint(scale);
}

bool EmbeddedEditor::unhandledKey(const eacp::Graphics::KeyEvent&)
{
    return false;
}

} // namespace MakeASound
