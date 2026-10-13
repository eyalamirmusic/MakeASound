#pragma once

// Objective-C++ only.

#include "../Parameters/ParameterList.h"

#import <AudioToolbox/AudioToolbox.h>

namespace MakeASound::AUv3
{

// The tree of the list's host-exposed entries, in declaration order: each named
// group an AUParameterGroup whose identifier is its id path, each entry an
// AUParameter with its id path as identifier (the v2 bridge hashes it into the
// AudioUnitParameterID, so it must be as stable as the id), its 31-bit host id as
// address, its display name, plain range and current value, and the unit, flags
// and value strings the v2 adapter gives it. The implementor blocks are the
// adapter's to set.
AUParameterTree* makeParameterTree(const ParameterList& list);

} // namespace MakeASound::AUv3
