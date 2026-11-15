#pragma once

#include "DeviceInfo.h"

#include <cstdint>
#include <string>

namespace MakeASound
{

// What macOS needs before miniaudio can record what is playing: a Core Audio process
// tap on every process's output (macOS 14.2+), wrapped in a private aggregate device
// that miniaudio opens as an ordinary capture device by its UID. Elsewhere create()
// returns NOT_SUPPORTED: Windows and PulseAudio record the output through miniaudio
// directly.
//
// macOS asks the user for "System Audio Recording Only" permission (Privacy &
// Security > Screen & System Audio Recording), worded from the
// NSAudioCaptureUsageDescription in the Info.plist of the responsible process: the
// app, or for a command-line tool the terminal that started it (a tool started by
// launchd answers for itself, from an Info.plist linked in with
// -sectcreate __TEXT __info_plist). Without the permission the tap records silence
// rather than failing.
class SystemOutputTap
{
public:
    SystemOutputTap() = default;
    SystemOutputTap(const SystemOutputTap&) = delete;
    SystemOutputTap& operator=(const SystemOutputTap&) = delete;
    ~SystemOutputTap();

    Error create();
    void destroy();

    bool isCreated() const;
    const std::string& getCaptureDeviceUid() const;

    // The tap is a stereo mixdown of everything playing.
    static constexpr auto channelCount = 2;

private:
    std::uint32_t tapId = 0;
    std::uint32_t aggregateDeviceId = 0;
    std::string captureDeviceUid;
};

} // namespace MakeASound
