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
    auto config = manager.getDefaultDuplexConfig();

    if (config.output.has_value())
        check(config.output->device.hasChannels(false));

    if (config.input.has_value())
        check(config.input->device.hasChannels(true));

    check(config.sampleRate > 0);
};

auto tDirectionalConfigs = test("DeviceManager/asksForOneDirectionAtATime") = []
{
    // A playback app that claims the capture side pays for a microphone permission
    // it never wanted, so neither one-way config fills in the other half.
    auto manager = DeviceManager {};

    check(!manager.getDefaultOutputConfig().input.has_value());
    check(!manager.getDefaultInputConfig().output.has_value());
};

auto tBackends = test("DeviceManager/offersTheDriverItIsRunningOn") = []
{
    // A machine that got a context at all is on one of the APIs it reports, so the
    // driver dropdown always has a selected item. A headless runner where no backend
    // came up reports Unknown and an empty list, which is consistent too.
    auto manager = DeviceManager {};
    auto backends = manager.getAvailableBackends();

    if (manager.getBackend() == MakeASound::Backend::Unknown)
        check(backends.empty());
    else
        check(backends.contains(manager.getBackend()));
};

auto tSwitchBackend = test("DeviceManager/dropsTheConfigWhenTheDriverChanges") = []
{
    // Switching APIs invalidates every device id the host is holding, so the manager
    // comes back stopped with nothing configured rather than carrying a stale config
    // across. Run against whatever this machine is already on: the switch has to be a
    // clean round trip even when the destination is where we started.
    auto manager = DeviceManager {};
    auto backend = manager.getBackend();

    if (backend == MakeASound::Backend::Unknown)
        return;

    manager.start(manager.getDefaultOutputConfig(), [](auto&) {});

    check(manager.setBackend(backend) == Error::NoError);
    check(!manager.isRunning());
    check(manager.getBackend() == backend);

    // And it is usable afterwards - a host re-opens by asking for the new default.
    manager.start(manager.getDefaultOutputConfig(), [](auto&) {});
    manager.stop();
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
    check(manager.getStreamBlockSize() == 0);
    check(manager.getStreamLatency() == 0);
};

auto tMidiDoesNotThrow = test("MidiManager/reportsFailuresWithoutThrowing") = []
{
    // The audio side returns an Error for a device that isn't there; the MIDI side
    // used to print to stderr and throw for the same kind of failure.
    auto midi = MakeASound::MidiManager {};

    check(midi.openInput(9999) != Error::NoError);
    check(!midi.isInputOpen(9999));

    // Nothing is open, so this is a send into the void rather than a silent success.
    check(midi.sendMessage(MakeASound::MIDI::Event::noteOn(0, 60, 1.f))
          == Error::INVALID_USE);
};
} // namespace
