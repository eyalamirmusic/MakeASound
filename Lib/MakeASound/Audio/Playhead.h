#pragma once

#include <cstdint>

namespace MakeASound
{

struct TimeSignature
{
    int numerator = 4;
    int denominator = 4;
};

// The host's transport for the current block. Fields the host does not supply
// keep their defaults, so a processor reads them unconditionally; `isValid` is
// false where there is no host at all (a device stream, a standalone app).
struct Playhead
{
    bool isValid = false;

    bool isPlaying = false;
    bool isRecording = false;
    bool isLooping = false;

    // Samples since project start.
    int64_t sampleTime = 0;

    // Quarter notes since project start, and the quarter note the current bar
    // began on.
    double ppqPosition = 0.0;
    double barStartPpq = 0.0;

    double bpm = 120.0;
    TimeSignature timeSignature {};

    // In quarter notes; meaningful only while `isLooping`.
    double loopStartPpq = 0.0;
    double loopEndPpq = 0.0;
};

} // namespace MakeASound
