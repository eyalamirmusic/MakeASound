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

// Backend::Unknown has no miniaudio counterpart — it means "no particular API" — so
// callers ask for the default backend order instead of passing a list. Anything else
// maps one to one; ma_backend_null is the fallback for a value out of range.
ma_backend getMaBackend(Backend backend);

// The APIs this build can actually reach on this machine, in miniaudio's own priority
// order. Compiled-in is not enough — a Windows build knows about WASAPI on a machine
// whose audio service is down, and a Linux one knows about JACK with no server running
// — so each candidate is initialised once and kept only if it comes up.
//
// The Null backend is left out: it is a silent stand-in miniaudio falls back to, not
// something to offer a user as a driver.
Vector<Backend> probeAvailableBackends();

// Walks an ma_device_info's nativeDataFormats and returns the unique
// sample rates the device exposes. A native-format entry with
// sampleRate==0 means "any rate supported"; in that case the full set
// of ma_standard_sample_rate values is returned.
Vector<int> collectSampleRates(const ma_device_info& info);

// nativeDataFormats has no explicit "preferred" entry. Picks 48000 if
// supported, else 44100, else the first rate in the list.
int pickPreferredSampleRate(const Vector<int>& rates);

} // namespace MakeASound::MiniAudio
