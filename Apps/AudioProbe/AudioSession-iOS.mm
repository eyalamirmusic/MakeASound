#include "AudioSession.h"

#import <Foundation/Foundation.h>

namespace AudioProbe
{

bool hasMicUsageDescription()
{
    @autoreleasepool
    {
        return [[NSBundle mainBundle]
                   objectForInfoDictionaryKey:@"NSMicrophoneUsageDescription"]
               != nil;
    }
}

} // namespace AudioProbe
