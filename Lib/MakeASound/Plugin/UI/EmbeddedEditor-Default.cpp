#include "EmbeddedEditor.h"

namespace MakeASound
{

// The host speaks points, and AppKit (UIKit) scales the backing store itself.
float EmbeddedEditor::unitScale() const noexcept
{
    return 1.f;
}

void EmbeddedEditor::applyScale() noexcept
{
}

// The responder chain already goes on from the embedded view into the host's.
bool EmbeddedEditor::unhandledKey(const eacp::Graphics::KeyEvent&)
{
    return false;
}

} // namespace MakeASound
