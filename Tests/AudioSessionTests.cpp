// Tests for which session options reach the platform. Pure decision logic: iOS is
// the only platform with a session, but what it accepts is decided here, so it runs
// anywhere the library builds - the simulator accepts what a phone refuses, so a
// phone is the only other place this would show.

#include <MakeASound/Devices/AudioSession.h>

#include <NanoTest/NanoTest.h>

using namespace nano;
using MakeASound::SessionCategory;
using MakeASound::SessionOptions;

namespace
{
auto tPlaybackOptions =
    test("AudioSession/outputOnlyCategoriesAskForNothingTheyCannotTake") = []
{
    // The defaults allow Bluetooth and AirPlay. A phone refuses setCategory for
    // Playback with either (Bluetooth HFP is input, A2DP and AirPlay are implicit
    // there and cannot be set), so the stream never opened: INVALID_PARAMETER from
    // start(). Only mixing and ducking survive in Playback.
    auto requested = SessionOptions {};
    requested.mixWithOthers = true;

    auto playback =
        MakeASound::getOptionsForCategory(requested, SessionCategory::Playback);

    check(playback.mixWithOthers);
    check(!playback.allowBluetooth);
    check(!playback.allowAirPlay);
    check(!playback.defaultToSpeaker);

    auto ambient =
        MakeASound::getOptionsForCategory(requested, SessionCategory::Ambient);

    check(!ambient.mixWithOthers);
    check(!ambient.allowBluetooth);
    check(!ambient.allowAirPlay);
};

auto tCaptureOptions =
    test("AudioSession/captureCategoriesKeepTheRoutesTheyCanUse") = []
{
    auto requested = SessionOptions {};
    requested.mixWithOthers = true;
    requested.duckOthers = true;

    auto duplex =
        MakeASound::getOptionsForCategory(requested, SessionCategory::PlayAndRecord);

    check(duplex.mixWithOthers);
    check(duplex.duckOthers);
    check(duplex.defaultToSpeaker);
    check(duplex.allowBluetooth);
    check(duplex.allowAirPlay);

    // Record takes a Bluetooth microphone, and nothing about output or mixing.
    auto record =
        MakeASound::getOptionsForCategory(requested, SessionCategory::Record);

    check(record.allowBluetooth);
    check(!record.allowAirPlay);
    check(!record.mixWithOthers);
    check(!record.duckOthers);
    check(!record.defaultToSpeaker);
};
} // namespace
