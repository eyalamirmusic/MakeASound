// Tests for the header-only realtime helpers of the device library: the
// parameter smoother and the denormal guard.

#include <MakeASound/MakeASound.h>

#include <NanoTest/NanoTest.h>

#include <cmath>

using namespace nano;

using MakeASound::Buffer;
using MakeASound::ScopedNoDenormals;
using MakeASound::Smoother;

// Tests live in an anonymous namespace: NanoTest registers a case by
// constructing a namespace-scope variable, so two files naming one the same way
// would otherwise collide at link time.
namespace
{
constexpr auto sampleRate = 48000;
constexpr auto rampSeconds = 0.02f;
constexpr auto rampSamples = 960;

Smoother makeSmoother(float seconds, float start)
{
    auto smoother = Smoother();
    smoother.setSampleRate(sampleRate);
    smoother.setRampTime(seconds);
    smoother.reset(start);
    return smoother;
}

// 1e-40 is representable only as a subnormal; volatile keeps the multiply at run
// time, where the FPU mode applies, instead of folded at compile time.
float denormalProduct() noexcept
{
    volatile auto denormal = 1e-40f;
    volatile auto unity = 1.0f;
    return denormal * unity;
}

auto tResetJumps = test("Smoother/resetLandsWithNoGlide") = []
{
    auto smoother = makeSmoother(rampSeconds, 0.2f);
    smoother.setTarget(0.8f);
    check(smoother.isSmoothing());

    smoother.reset(0.5f);
    check(!smoother.isSmoothing());
    check(smoother.next() == 0.5f);
    check(smoother.getTarget() == 0.5f);
};

auto tArrivesWithinRamp = test("Smoother/arrivesWithinItsRampAndHolds") = []
{
    struct Ramp
    {
        float start, target;
    };

    const Ramp ramps[] = {
        {0.f, 1.f},
        {0.f, -1.f},
        {1.f, 0.f},
        {10.f, 9.f},
        {1000.f, 999.f},
        {-999.f, -1000.f},
        {0.f, 0.001f},
    };

    for (auto ramp: ramps)
    {
        auto smoother = makeSmoother(rampSeconds, ramp.start);
        smoother.setTarget(ramp.target);

        check(smoother.next() != ramp.target);

        for (auto i = 1; i < rampSamples; ++i)
            smoother.next();

        check(smoother.getCurrent() == ramp.target);

        for (auto i = 0; i < rampSamples; ++i)
            check(smoother.next() == ramp.target);

        check(!smoother.isSmoothing());
    }
};

auto tRampIsMonotonic = test("Smoother/rampIsMonotonicAndNeverJumps") = []
{
    const float targets[] = {1.f, -1.f, 9.f, 999.f};

    for (auto target: targets)
    {
        auto smoother = makeSmoother(rampSeconds, 0.f);
        smoother.setTarget(target);

        auto previous = smoother.getCurrent();
        auto first = std::abs(smoother.next() - previous);
        check(first > 0.f);

        for (auto i = 1; i < rampSamples; ++i)
        {
            previous = smoother.getCurrent();
            auto step = std::abs(smoother.next() - previous);

            check(step <= first);
            check(std::abs(smoother.getCurrent()) <= std::abs(target));
        }
    }
};

auto tRetargetSameValue =
    test("Smoother/repeatingTheTargetDoesNotRestartTheRamp") = []
{
    auto smoother = makeSmoother(rampSeconds, 0.f);

    for (auto i = 0; i < rampSamples; ++i)
    {
        smoother.setTarget(1.f);
        smoother.next();
    }

    check(smoother.getCurrent() == 1.f);
};

auto tZeroRampJumps = test("Smoother/zeroLengthRampJumps") = []
{
    auto smoother = makeSmoother(0.f, 0.f);
    smoother.setTarget(0.7f);

    check(smoother.next() == 0.7f);
    check(!smoother.isSmoothing());
};

auto tBlockHelpers = test("Smoother/fillAndApplyGainStepOncePerSample") = []
{
    auto ramp = makeSmoother(rampSeconds, 0.f);
    auto gain = makeSmoother(rampSeconds, 0.f);
    ramp.setTarget(1.f);
    gain.setTarget(1.f);

    auto expected = Buffer(1, 64);
    ramp.fill(expected[0]);

    auto buffer = Buffer(2, 64);
    buffer.fill(1.f);
    gain.applyGain(buffer);

    for (auto s = 0; s < 64; ++s)
    {
        check(buffer[0][s] == expected[0][s]);
        check(buffer[1][s] == expected[0][s]);
    }

    check(gain.getCurrent() == ramp.getCurrent());
};

auto tSettledGainIsFlat = test("Smoother/settledApplyGainIsAPlainMultiply") = []
{
    auto smoother = makeSmoother(rampSeconds, 0.5f);

    auto buffer = Buffer(2, 16);
    buffer.fill(2.f);
    smoother.applyGain(buffer);

    for (auto channel: buffer)
        for (auto sample: channel)
            check(sample == 1.f);
};

auto tFlushes = test("ScopedNoDenormals/flushesDenormalsToZero") = []
{
#if defined(MAKEASOUND_DENORMALS_X86) || defined(MAKEASOUND_DENORMALS_ARM64)
    // Stored while the guard is up, so the multiply cannot sink past its end.
    volatile auto flushed = 1.f;
    {
        auto guard = ScopedNoDenormals();
        flushed = denormalProduct();
    }

    check(flushed == 0.f);
#endif
};

auto tRestores = test("ScopedNoDenormals/restoresTheCallersMode") = []
{
    auto before = denormalProduct();
    {
        auto guard = ScopedNoDenormals();
    }

    check(denormalProduct() == before);
};

auto tNests = test("ScopedNoDenormals/innerGuardKeepsTheOuterOne") = []
{
#if defined(MAKEASOUND_DENORMALS_X86) || defined(MAKEASOUND_DENORMALS_ARM64)
    auto outer = ScopedNoDenormals();
    {
        auto inner = ScopedNoDenormals();
        check(denormalProduct() == 0.f);
    }

    check(denormalProduct() == 0.f);
#endif
};
} // namespace
