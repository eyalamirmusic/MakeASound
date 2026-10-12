#include "View.h"

#include <eacp/Graphics/Window/EmbeddedView.h>

#import <AppKit/AppKit.h>

namespace MakeASound::AAX
{

AAX_EViewContainer_Type View::nativeContainerType() noexcept
{
    return AAX_eViewContainer_Type_NSView;
}

// The container may be any size when it is handed over and is sized after
// GetViewSize, so filling it would grow the surface by whatever it grew; the
// surface keeps its size and its distance from the container's top instead.
void View::pinToTopLeft() noexcept
{
    auto* surface = (__bridge NSView*) embedded->getHandle();
    auto* container = surface.superview;
    auto flipped = container != nil && container.isFlipped;
    surface.autoresizingMask = flipped ? NSViewMaxYMargin : NSViewMinYMargin;
}

} // namespace MakeASound::AAX
