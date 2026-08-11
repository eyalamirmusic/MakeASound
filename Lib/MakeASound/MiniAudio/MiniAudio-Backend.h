#pragma once

#include <miniaudio.h>

#include "../Common/Common.h"
#include "../Devices/DeviceInfo.h"

namespace MakeASound::MiniAudio
{

Error getError(ma_result result);
AudioCallbackStatus getStatus(ma_result result);
DeviceNotification getNotification(ma_device_notification_type type);

Backend getBackend(ma_backend backend);

// Backend::Unknown has no counterpart: callers ask for the default backend order
// instead of passing a list. Out-of-range values fall back to ma_backend_null.
ma_backend getMaBackend(Backend backend);

// Compiled-in is not enough — a build knows about WASAPI on a machine whose audio
// service is down — so each candidate is initialised once and kept only if it comes
// up. Null is left out: miniaudio's silent stand-in, not a driver to offer a user.
Vector<Backend> probeAvailableBackends();

// A native-format entry with sampleRate==0 means "any rate supported", in which
// case the full set of standard rates is returned.
Vector<int> collectSampleRates(const ma_device_info& info);

// nativeDataFormats has no "preferred" entry, so one is picked by convention.
int pickPreferredSampleRate(const Vector<int>& rates);

} // namespace MakeASound::MiniAudio
