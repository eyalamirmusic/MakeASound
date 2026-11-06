#pragma once

// Objective-C++ only.

#include "../AU/HostParameters.h"
#include "../Parameters/ParameterList.h"

#import <AudioToolbox/AudioToolbox.h>

namespace MakeASound::AUv3
{

// The tree of the entries `space` lists, in declaration order: each named group
// an AUParameterGroup with its EnclosingGroup::identifier, each entry an
// AUParameter with its Entry::identifier (the v2 bridge hashes it into the
// AudioUnitParameterID, so it must be as stable as the id) and Entry::address,
// its display name, and the range, unit, flags and value strings `space` gives
// it. MakeASound's scheme: the id paths, the 31-bit host id, the v2 adapter's
// info and the current value. A JUCE scheme: JUCE's tree (AUv3.mm
// createParameter), the leaf keys, the 64-bit hashCode64 addresses, every
// parameter but the added bypass (a non-automatable one NonRealTime), the
// range of Compat::juceAUParameterInfo, and as its first value the normalized
// default, as JUCE seeds it. The implementor blocks are the adapter's to set.
AUParameterTree* makeParameterTree(const ParameterList& list,
                                   const AU::ParameterSpace& space = {});

} // namespace MakeASound::AUv3
