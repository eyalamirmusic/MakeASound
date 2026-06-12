#pragma once

#include <miniaudio.h>

#include "../Common/Common.h"
#include "../Devices/DeviceInfo.h"

namespace MakeASound::MiniAudio
{

Error getError(ma_result result);
AudioCallbackStatus getStatus(ma_result result);

// Walks an ma_device_info's nativeDataFormats and returns the unique
// sample rates the device exposes. A native-format entry with
// sampleRate==0 means "any rate supported"; in that case the full set
// of ma_standard_sample_rate values is returned.
Vector<int> collectSampleRates(const ma_device_info& info);

// nativeDataFormats has no explicit "preferred" entry. Picks 48000 if
// supported, else 44100, else the first rate in the list.
int pickPreferredSampleRate(const Vector<int>& rates);

} // namespace MakeASound::MiniAudio
