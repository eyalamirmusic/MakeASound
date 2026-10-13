#pragma once

// Objective-C++ only.

#import <Foundation/Foundation.h>
#import <AudioToolbox/AudioToolbox.h>

#include <optional>
#include <string>
#include <string_view>

namespace MakeASound::AUv3
{

// The ClassInfo-shaped dictionary a v2 unit saves (type, subtype, manufacturer,
// version) plus the plugin's document under MakeASoundState, as NSData.
NSDictionary<NSString*, id>* makeState(const AudioComponentDescription& description,
                                       UInt32 version,
                                       std::string_view document);

// The document, or nullopt when the dictionary is another component's or carries
// none of ours.
std::optional<std::string> readState(NSDictionary<NSString*, id>* state,
                                     const AudioComponentDescription& description);

} // namespace MakeASound::AUv3
