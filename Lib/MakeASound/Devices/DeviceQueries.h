#pragma once

#include "../Common/Common.h"
#include "DeviceInfo.h"

#include <optional>
#include <string>

namespace MakeASound
{

// What a device runs at, where the platform can say without opening anything.
struct NativeFormat
{
    int channels {};

    // What the route is at now.
    int sampleRate {};

    // What it will accept; empty means sampleRate is the only answer available.
    Vector<int> sampleRates;
};

// iOS answers from AVAudioSession — one route, one format — which keeps enumeration
// away from CoreAudio's RemoteIO unit, whose RPC to the audio daemon aborts the
// process when it times out. nullopt elsewhere: the backend's own query is safe.
std::optional<NativeFormat> getNativeFormat(bool input);

// Powers of two the device can run. macOS asks CoreAudio, matched by device name
// since DeviceInfo::id is a MakeASound index, not an AudioDeviceID; other platforms,
// and a failed query, return a conservative 64..2048 fallback.
Vector<int> getSupportedBlockSizes(const DeviceInfo& device);

// What the platform itself calls its default device, where it will say; empty
// otherwise. Backends disagree with it: Core Audio marks every capture device that
// belongs to a duplex unit as default, so the platform's own answer is what settles
// which one a caller meant. Matched by name, as above.
std::string getDefaultDeviceName(bool input);

// What the route adds on top of whatever the stream itself buffers: the device's own
// latency, its safety offset and its stream latency on macOS, the session's reported
// latency on iOS. In frames at the rate the device is running; 0 where the platform
// will not say, which leaves the caller reporting only the buffering it can see.
int getRouteLatency(const DeviceInfo& device, bool input);

// The rate the device is running at right now — any app can move a shared device, so
// this changes without MakeASound doing anything. Matched by name as above. 0 where
// unavailable; callers fall back to DeviceInfo::preferredSampleRate.
int getCurrentSampleRate(const DeviceInfo& device);

} // namespace MakeASound
