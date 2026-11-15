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
// The first create() in a process makes macOS ask the user for "System Audio
// Recording Only" permission, worded from the app's NSAudioCaptureUsageDescription.
// Until it is granted the tap delivers silence rather than failing.
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
