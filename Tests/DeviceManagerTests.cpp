// Tests for what the audio facade does when there is nothing to open. Unlike the
// DeviceInfo tests these build a real DeviceManager, so they touch the backend - but
// they never ask for a device that has to exist, which makes them safe on a headless
// CI runner with no audio hardware at all. That is the case they are here for.

#include <MakeASound/MakeASound.h>

#include <NanoTest/NanoTest.h>

using namespace nano;
using MakeASound::DeviceManager;
using MakeASound::Error;
using MakeASound::StreamConfig;

namespace
{
auto tEmptyConfig =
    test("DeviceManager/reportsAStreamWithNoDeviceInsteadOfThrowing") = []
{
    // The shape a machine with no audio device produces: a config naming neither a
    // playback nor a capture side. Opening it cannot work, and the whole point is
    // that saying so is a return value rather than an exception through the caller's
    // constructor.
    auto manager = DeviceManager {};
    auto error = manager.start(StreamConfig {}, [](auto&) {});

    check(error != Error::NoError);
    check(!manager.isRunning());
    check(manager.getLastError() == error);
    check(!MakeASound::getErrorMessage(error).empty());
};

auto tNoCallback = test("DeviceManager/refusesToOpenAStreamWithNoCallback") = []
{
    auto manager = DeviceManager {};

    check(manager.setConfig(StreamConfig {}) == Error::INVALID_USE);
    check(!manager.isRunning());
};

auto tDefaultConfig = test("DeviceManager/onlyFillsInSidesThatExist") = []
{
    // Whatever this machine has. A side that is present has to name a device with
    // channels in that direction - a blank one there is what asks the backend for a
    // duplex stream on hardware that only goes one way, and fails the whole open.
    auto manager = DeviceManager {};
    auto config = manager.getDefaultConfig();

    if (config.output.has_value())
        check(config.output->device.hasChannels(false));

    if (config.input.has_value())
        check(config.input->device.hasChannels(true));

    check(config.sampleRate > 0);
};

auto tStopsCleanly = test("DeviceManager/stopsCleanlyAfterAFailedOpen") = []
{
    // A failed start leaves the manager usable: stopping it, asking it questions and
    // tearing it down all have to work, since a host that shows "no audio device" goes
    // on running around one.
    auto manager = DeviceManager {};

    manager.start(StreamConfig {}, [](auto&) {});
    manager.stop();

    check(!manager.isRunning());
    check(manager.getStreamSampleRate() == 0);
    check(manager.getStreamLatency() == 0);
};
} // namespace
