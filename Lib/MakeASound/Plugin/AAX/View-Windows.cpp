#include "View.h"

namespace MakeASound::AAX
{

AAX_EViewContainer_Type View::nativeContainerType() noexcept
{
    return AAX_eViewContainer_Type_HWND;
}

// Nothing to pin: eacp's EmbeddedView creates a WS_CHILD window at (0,0) of the
// container's HWND, and a child window keeps its position relative to its
// parent's top-left however the parent is resized, so the surface stays where
// it was put at the size last set.
void View::pinToTopLeft() noexcept
{
}

} // namespace MakeASound::AAX
