#pragma once

// The SDK's headers include <expected>, so every TU that includes this one is
// C++23; nothing in MakeASoundPlugin ever does.
#include <AudioUnitSDK/AUMIDIEffectBase.h>
#include <AudioUnitSDK/MusicDeviceBase.h>

namespace MakeASound::AU
{
namespace au = ausdk;
}
