#include "AUv3ViewTestHost.h"

#import <Cocoa/Cocoa.h>

CGSize AUv3ViewHost::sizeOf(void* nativeView)
{
    return ((__bridge NSView*) nativeView).bounds.size;
}

void AUv3ViewHost::hostResize(AUViewController* controller, CGSize size)
{
    [controller.view setFrameSize:size];
    controller.view.needsLayout = YES;
    [controller.view layoutSubtreeIfNeeded];
}
