#pragma once

// Objective-C++ only.

#import <Foundation/Foundation.h>

#include <string_view>

namespace MakeASound::AUv3
{

inline NSString* toNSString(std::string_view text)
{
    return [[NSString alloc] initWithBytes:text.data()
                                    length:text.size()
                                  encoding:NSUTF8StringEncoding];
}

} // namespace MakeASound::AUv3
