#include "AUv3ViewTestHost.h"

#import <UIKit/UIKit.h>

CGSize AUv3ViewHost::sizeOf(void* nativeView)
{
    return ((__bridge UIView*) nativeView).bounds.size;
}

void AUv3ViewHost::hostResize(AUViewController* controller, CGSize size)
{
    controller.view.frame = CGRectMake(0, 0, size.width, size.height);
    [controller.view layoutIfNeeded];
}
