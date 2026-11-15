// miniaudio's PulseAudio device-info lookup for a device that is gone, which
// crashed miniaudio until the fork CMake/FindMiniaudio.cmake fetches. A machine
// with no PulseAudio server (or PipeWire's) has nothing to look up, and the case
// returns without asserting.

#include <miniaudio.h>

#include <NanoTest/NanoTest.h>

#include <cstring>

namespace PulseDeviceInfoTests
{
using namespace nano;

namespace
{
auto tGone = test("PulseDeviceInfo/aDeviceThatIsGoneIsNoDevice") = []
{
    auto backend = ma_backend_pulseaudio;
    auto context = ma_context {};

    if (ma_context_init(&backend, 1, nullptr, &context) != MA_SUCCESS)
        return;

    // libpulse answers it as it answers a device unplugged between enumeration
    // and lookup: an error, and no info.
    auto id = ma_device_id {};
    std::strncpy(id.pulse, "makeasound.tests.no-such-device", sizeof(id.pulse) - 1);

    for (auto type: {ma_device_type_playback, ma_device_type_capture})
    {
        auto info = ma_device_info {};
        check(ma_context_get_device_info(&context, type, &id, &info)
              == MA_NO_DEVICE);
    }

    ma_context_uninit(&context);
};
} // namespace
} // namespace PulseDeviceInfoTests
