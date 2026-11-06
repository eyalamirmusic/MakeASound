#pragma once

/** @file
    The AudioUnitSDK headers the Audio Unit adapter includes. The SDK's headers
    include `<expected>`, so every source file that includes this one is C++23;
    nothing in MakeASoundPlugin ever does.
*/

#include <AudioUnitSDK/AUMIDIEffectBase.h>
#include <AudioUnitSDK/MusicDeviceBase.h>

namespace MakeASound::AU
{
namespace au = ausdk;
}
