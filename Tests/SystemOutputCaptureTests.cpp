// Records what this machine is playing while the case itself plays a tone, and checks
// the tone is in the recording. Returns without asserting where there is nothing to
// play through, or where the driver cannot record its output (NOT_SUPPORTED).
//
// On macOS the process needs "System Audio Recording Only" permission (System
// Settings > Privacy & Security > Screen & System Audio Recording), or the tap
// records silence and the case fails.

#include <MakeASound/MakeASound.h>

#include <NanoTest/NanoTest.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <thread>

namespace SystemOutputCaptureTests
{
using namespace nano;
using MakeASound::AudioCallbackInfo;
using MakeASound::DeviceManager;
using MakeASound::Error;

namespace
{
constexpr auto toneFrequency = 1000.0;
constexpr auto toneAmplitude = 0.2f;
constexpr auto listeningTime = std::chrono::milliseconds(1500);

// Well above a silent tap's zero, well below the tone's level after any system
// volume a test machine is likely to run at.
constexpr auto minimumHeardToneLevel = 0.005;

// Goertzel: the amplitude of one frequency in a block, as a fraction of full scale.
double measureToneLevel(MakeASound::Channel samples, double frequency, int sampleRate)
{
    const auto omega = 2.0 * std::numbers::pi * frequency / sampleRate;
    const auto coefficient = 2.0 * std::cos(omega);
    auto previous = 0.0;
    auto beforePrevious = 0.0;

    for (auto sample: samples)
    {
        const auto current = sample + coefficient * previous - beforePrevious;
        beforePrevious = previous;
        previous = current;
    }

    const auto power = previous * previous + beforePrevious * beforePrevious
                       - coefficient * previous * beforePrevious;
    const auto numSamples = static_cast<double>(samples.size());
    return 2.0 * std::sqrt(std::max(power, 0.0)) / std::max(numSamples, 1.0);
}

auto tHearsItsOwnTone =
    test("SystemOutputCapture/recordsAToneThisProcessPlays") = []
{
    auto player = DeviceManager {};
    auto playConfig = player.getDefaultOutputConfig();

    if (!playConfig.output.has_value())
    {
        std::puts("skipped: no output device");
        return;
    }

    auto recorder = DeviceManager {};
    auto recordConfig = recorder.getDefaultSystemOutputCaptureConfig();
    auto loudestTone = std::atomic<double> {0.0};

    auto recordError = recorder.start(
        recordConfig,
        [&](AudioCallbackInfo& info)
        {
            for (auto channel: info.getInput())
            {
                auto level = measureToneLevel(channel, toneFrequency, info.sampleRate);

                if (level > loudestTone.load())
                    loudestTone = level;
            }
        });

    if (recordError == Error::NOT_SUPPORTED)
    {
        std::puts("skipped: this driver cannot record its output");
        return;
    }

    check(recordError == Error::NoError, MakeASound::getErrorMessage(recordError));

    auto phase = 0.0;
    auto playError = player.start(
        playConfig,
        [&](AudioCallbackInfo& info)
        {
            const auto phaseStep =
                2.0 * std::numbers::pi * toneFrequency / info.sampleRate;
            auto output = info.getOutput();

            for (auto frame = 0; frame < info.numSamples; ++frame)
            {
                const auto value = toneAmplitude * static_cast<float>(std::sin(phase));
                phase = std::fmod(phase + phaseStep, 2.0 * std::numbers::pi);

                for (auto channel: output)
                    channel[frame] = value;
            }
        });

    check(playError == Error::NoError, MakeASound::getErrorMessage(playError));

    std::this_thread::sleep_for(listeningTime);
    player.stop();
    recorder.stop();

    std::printf("loudest 1 kHz level recorded: %f\n", loudestTone.load());
    check(loudestTone.load() > minimumHeardToneLevel);
};

auto tConfigShape =
    test("SystemOutputCapture/defaultConfigRecordsTheOutputDevice") = []
{
    auto manager = DeviceManager {};
    auto config = manager.getDefaultSystemOutputCaptureConfig();

    check(config.captureSystemOutput);
    check(!config.output.has_value());

    if (config.input.has_value())
        check(config.input->nChannels == config.input->device.outputChannels);
};
} // namespace
} // namespace SystemOutputCaptureTests
