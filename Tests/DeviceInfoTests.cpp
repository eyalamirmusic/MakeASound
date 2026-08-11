// Tests for the pure decision logic in DeviceInfo - channel defaults, sample
// rate negotiation, and the AudioCallbackInfo accessors. None of it touches a
// backend or opens a device, so it runs anywhere the library builds.

#include <MakeASound/Devices/DeviceInfo.h>

#include <NanoTest/NanoTest.h>

#include <array>

using namespace nano;
using MakeASound::AudioCallbackInfo;
using MakeASound::Backend;
using MakeASound::DeviceInfo;
using MakeASound::StreamConfig;
using MakeASound::StreamParameters;

// Tests live in an anonymous namespace: NanoTest registers a case by
// constructing a namespace-scope variable, so two files naming one the same way
// would otherwise collide at link time.
namespace
{
DeviceInfo makeDevice(MakeASound::Vector<int> rates, int preferred, int channels)
{
    auto device = DeviceInfo {};
    device.sampleRates = std::move(rates);
    device.preferredSampleRate = preferred;
    device.outputChannels = channels;
    device.inputChannels = channels;

    return device;
}

// ---------------------------------------------------------------------------
// Driver names
// ---------------------------------------------------------------------------

auto tBackendNames = test("Backend/everyDriverHasAName") = []
{
    // Every value but Unknown, which names no particular API. A missing entry would
    // show up as a blank item in a driver dropdown rather than as a build error.
    constexpr auto backends = std::array {Backend::WASAPI,
                                          Backend::DirectSound,
                                          Backend::WinMM,
                                          Backend::CoreAudio,
                                          Backend::SndIO,
                                          Backend::Audio4,
                                          Backend::OSS,
                                          Backend::PulseAudio,
                                          Backend::ALSA,
                                          Backend::JACK,
                                          Backend::AAudio,
                                          Backend::OpenSL,
                                          Backend::WebAudio,
                                          Backend::Null};

    for (auto backend: backends)
    {
        auto name = MakeASound::getBackendName(backend);

        check(!name.empty());
        check(MakeASound::getBackendFromName(name) == backend);
    }

    check(MakeASound::getBackendName(Backend::Unknown).empty());
};

auto tBackendFromName = test("Backend/parsesADriverNameAsAUserWouldWriteIt") = []
{
    // Case, spacing and punctuation are the user's business, not the parser's.
    check(MakeASound::getBackendFromName("Core Audio") == Backend::CoreAudio);
    check(MakeASound::getBackendFromName("coreaudio") == Backend::CoreAudio);
    check(MakeASound::getBackendFromName("core-audio") == Backend::CoreAudio);
    check(MakeASound::getBackendFromName("wasapi") == Backend::WASAPI);

    // The enumerator's own spelling, which is what a serialized DeviceInfo carries.
    check(MakeASound::getBackendFromName("DirectSound") == Backend::DirectSound);

    check(!MakeASound::getBackendFromName("").has_value());
    check(!MakeASound::getBackendFromName("not a driver").has_value());
};

// ---------------------------------------------------------------------------
// Channel defaults
// ---------------------------------------------------------------------------

auto tDefaultChannels = test("DeviceInfo/defaultChannelCountCapsAtStereo") = []
{
    auto device = DeviceInfo {};
    device.outputChannels = 8;
    device.inputChannels = 6;

    check(MakeASound::getDefaultNumChannels(device, false) == 2);
    check(MakeASound::getDefaultNumChannels(device, true) == 2);
};

auto tDefaultMono = test("DeviceInfo/defaultChannelCountFollowsAMonoDevice") = []
{
    auto device = DeviceInfo {};
    device.outputChannels = 1;
    device.inputChannels = 0;

    check(MakeASound::getDefaultNumChannels(device, false) == 1);
    check(MakeASound::getDefaultNumChannels(device, true) == 0);
};

// ---------------------------------------------------------------------------
// Devices that aren't there
// ---------------------------------------------------------------------------

auto tBlankIsInvalid = test("DeviceInfo/aBlankDeviceIsNotAValidOne") = []
{
    // What enumeration hands back when the machine has nothing of that kind.
    check(!DeviceInfo {}.isValid());
    check(!DeviceInfo {}.hasChannels(true));
    check(!DeviceInfo {}.hasChannels(false));
};

auto tChannelsPerDirection = test("DeviceInfo/knowsWhichDirectionsItCanRun") = []
{
    // A desktop with speakers and no microphone: valid to play out of, nothing to
    // record from.
    auto device = DeviceInfo {};
    device.outputChannels = 2;

    check(device.isValid());
    check(device.hasChannels(false));
    check(!device.hasChannels(true));
};

auto tErrorMessages = test("DeviceInfo/hasAMessageForEveryFailure") = []
{
    check(MakeASound::getErrorMessage(MakeASound::Error::NoError).empty());
    check(!MakeASound::getErrorMessage(MakeASound::Error::NO_DEVICES_FOUND).empty());
    check(!MakeASound::getErrorMessage(MakeASound::Error::UNKNOWN_ERROR).empty());
};

// ---------------------------------------------------------------------------
// Sample rate support / negotiation
// ---------------------------------------------------------------------------

auto tSupportsRate = test("DeviceInfo/reportsWhichSampleRatesItSupports") = []
{
    auto device = makeDevice({44100, 48000}, 48000, 2);

    check(MakeASound::deviceSupportsSampleRate(device, 44100));
    check(MakeASound::deviceSupportsSampleRate(device, 48000));
    check(!MakeASound::deviceSupportsSampleRate(device, 96000));
};

auto tPrefersOutput = test("DeviceInfo/sampleRatePrefersTheOutputsPreferred") = []
{
    auto output = makeDevice({44100, 48000}, 48000, 2);
    auto input = makeDevice({44100, 48000}, 44100, 2);

    check(MakeASound::pickCompatibleSampleRate(output, input) == 48000);
};

auto tFallsToInput = test("DeviceInfo/sampleRateFallsBackToTheInputsPreferred") = []
{
    // The output prefers a rate the input can't do, so the input's preferred
    // rate - which both support - wins.
    auto output = makeDevice({44100, 48000, 96000}, 96000, 2);
    auto input = makeDevice({44100, 48000}, 48000, 2);

    check(MakeASound::pickCompatibleSampleRate(output, input) == 48000);
};

auto tHighestCommon = test("DeviceInfo/sampleRateFallsBackToHighestCommon") = []
{
    // Neither side expresses a preference, so the best rate both can do wins.
    auto output = makeDevice({44100, 48000}, 0, 2);
    auto input = makeDevice({44100, 48000}, 0, 2);

    check(MakeASound::pickCompatibleSampleRate(output, input) == 48000);
};

auto tNoCommon = test("DeviceInfo/sampleRateFallsBackToOutputWhenNothingIsShared") =
    []
{
    // Nothing in common: the output is what actually has to run, so it wins.
    auto output = makeDevice({96000}, 96000, 2);
    auto input = makeDevice({44100}, 44100, 2);

    check(MakeASound::pickCompatibleSampleRate(output, input) == 96000);
};

auto tNoCommonNoPreferred =
    test("DeviceInfo/sampleRateFallsBackToTheOutputsFirstRate") = []
{
    auto output = makeDevice({96000}, 0, 2);
    auto input = makeDevice({44100}, 44100, 2);

    check(MakeASound::pickCompatibleSampleRate(output, input) == 96000);
};

auto tNothingKnown = test("DeviceInfo/sampleRateFallsBackTo44100WhenNothingIsKnown") =
    []
{
    auto output = DeviceInfo {};
    auto input = DeviceInfo {};

    check(MakeASound::pickCompatibleSampleRate(output, input) == 44100);
};

auto tInputOnly = test("DeviceInfo/sampleRateFollowsTheInputWhenThereIsNoOutput") = []
{
    // An input-only machine: with no output to negotiate against, the input's own
    // preference is the only informed answer left.
    auto output = DeviceInfo {};
    auto input = makeDevice({44100, 48000}, 48000, 2);

    check(MakeASound::pickCompatibleSampleRate(output, input) == 48000);
};

// ---------------------------------------------------------------------------
// StreamParameters / StreamConfig
// ---------------------------------------------------------------------------

auto tParamsDefault = test("StreamParameters/defaultsToTheDevicesChannelCount") = []
{
    auto device = DeviceInfo {};
    device.outputChannels = 8;

    auto params = StreamParameters {device, false};

    check(params.nChannels == 2);
    check(params.firstChannel == 0);
};

auto tParamsExplicit = test("StreamParameters/honoursAnExplicitChannelSlice") = []
{
    auto device = DeviceInfo {};
    device.outputChannels = 8;

    auto params = StreamParameters {device, false, 4, 2};

    check(params.nChannels == 4);
    check(params.firstChannel == 2);
};

auto tConfigChannels = test("StreamConfig/reportsZeroChannelsForAnAbsentSide") = []
{
    auto device = DeviceInfo {};
    device.outputChannels = 2;

    auto config = StreamConfig {};
    config.output = StreamParameters {device, false};

    check(config.getOutputChannels() == 2);
    check(config.getInputChannels() == 0);
};

// ---------------------------------------------------------------------------
// AudioCallbackInfo
// ---------------------------------------------------------------------------

auto tCallbackBuffers = test("AudioCallbackInfo/handsOutCorrectlyShapedBuffers") = []
{
    auto outputSamples = std::array<float, 8> {};
    auto inputSamples = std::array<float, 4> {};

    auto info = AudioCallbackInfo {};
    info.numOutputs = 2;
    info.numInputs = 1;
    info.numSamples = 4;
    info.outputBuffer = outputSamples.data();
    info.inputBuffer = inputSamples.data();

    auto output = info.getOutput();
    check(output.getNumChannels() == 2);
    check(output.getNumSamples() == 4);
    check(output.getChannelPointer(1) == outputSamples.data() + 4);

    auto input = info.getInput();
    check(input.getNumChannels() == 1);
    check(input.getNumSamples() == 4);
    check(input.getChannelPointer(0) == inputSamples.data());
};

auto tEqualityShape = test("AudioCallbackInfo/comparesOnlyTheStreamShape") = []
{
    // This is what drives the dirty flag: a block that differs only in
    // per-callback bookkeeping is *not* a shape change.
    auto first = AudioCallbackInfo {};
    first.numInputs = 2;
    first.numOutputs = 2;
    first.sampleRate = 48000;
    first.maxBlockSize = 512;

    auto second = first;
    second.streamTime = 12.5;
    second.latency = 999;
    second.dirty = true;
    second.errorCode = 3;
    second.numSamples = 64;

    check(first == second);
    check(!(first != second));
};

auto tEqualityDetectsChange = test("AudioCallbackInfo/detectsAStreamShapeChange") = []
{
    auto first = AudioCallbackInfo {};
    first.numInputs = 2;
    first.numOutputs = 2;
    first.sampleRate = 48000;
    first.maxBlockSize = 512;

    auto rateChanged = first;
    rateChanged.sampleRate = 44100;
    check(first != rateChanged);

    auto blockChanged = first;
    blockChanged.maxBlockSize = 256;
    check(first != blockChanged);

    auto channelsChanged = first;
    channelsChanged.numOutputs = 1;
    check(first != channelsChanged);
};
} // namespace
