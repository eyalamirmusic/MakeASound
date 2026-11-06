#pragma once

// Objective-C++ only. What the AUv3 view suite needs of the platform's view
// framework, one TU per platform: the size of a native view, and a host resizing
// the controller's view and laying it out as UIKit or AppKit would.

#import <CoreAudioKit/AUViewController.h>

namespace AUv3ViewHost
{

// A UIView* or an NSView*, as an eacp view's handle is.
CGSize sizeOf(void* nativeView);

void hostResize(AUViewController* controller, CGSize size);

} // namespace AUv3ViewHost
