// Tests for MakeASound::Buffer and MakeASound::Channel - the non-owning planar
// views a callback sees over its audio block. The block is channel-major, so
// what's worth pinning is the arithmetic Buffer does on the caller's behalf:
// where each channel starts, that writes land in the right place, and that the
// shape survives being read off a temporary (the C++20 lifetime case Buffer.h
// calls out).

#include <MakeASound/Audio/Buffer.h>

#include <NanoTest/NanoTest.h>

#include <array>
#include <vector>

using namespace nano;
using MakeASound::Buffer;
using MakeASound::Channel;
using MakeASound::Span;

// Tests live in an anonymous namespace: NanoTest registers a case by
// constructing a namespace-scope variable, so two files naming one the same way
// would otherwise collide at link time.
namespace
{
constexpr auto numChannels = 3;
constexpr auto numSamples = 4;

// A 3x4 planar block where channel c, sample s holds the value c * 10 + s, so
// any mix-up of channel/sample indexing shows up as an obviously wrong number.
struct PlanarBlock
{
    PlanarBlock() noexcept
    {
        for (auto channel = 0; channel < numChannels; ++channel)
            for (auto sample = 0; sample < numSamples; ++sample)
                samples[channel * numSamples + sample] =
                    static_cast<float>(channel * 10 + sample);
    }

    Buffer view() noexcept { return {samples.data(), numChannels, numSamples}; }

    std::array<float, numChannels * numSamples> samples {};
};

auto tShape = test("Buffer/reportsItsShape") = []
{
    auto block = PlanarBlock {};
    auto buffer = block.view();

    check(buffer.getNumChannels() == numChannels);
    check(buffer.getNumSamples() == numSamples);
    check(!buffer.isEmpty());
};

auto tDefaultEmpty = test("Buffer/defaultConstructedIsEmpty") = []
{
    auto buffer = Buffer {};

    check(buffer.isEmpty());
    check(buffer.getNumChannels() == 0);
    check(buffer.getNumSamples() == 0);
};

auto tChannelMajor = test("Buffer/laysChannelsOutChannelMajor") = []
{
    auto block = PlanarBlock {};
    auto buffer = block.view();

    // Channel c starts exactly c * numSamples into the flat block.
    for (auto channel = 0; channel < numChannels; ++channel)
        check(buffer.getChannelPointer(channel)
              == block.samples.data() + channel * numSamples);

    for (auto channel = 0; channel < numChannels; ++channel)
    {
        auto samples = buffer.getChannel(channel);

        check(samples.size() == numSamples);

        for (auto sample = 0; sample < numSamples; ++sample)
            check(samples[sample] == static_cast<float>(channel * 10 + sample));
    }
};

auto tAccessorsAgree = test("Buffer/getChannelAndSubscriptAgree") = []
{
    auto block = PlanarBlock {};
    auto buffer = block.view();

    for (auto channel = 0; channel < numChannels; ++channel)
    {
        check(buffer[channel].data() == buffer.getChannel(channel).data());
        check(buffer[channel].size() == buffer.getChannel(channel).size());
        check(buffer[channel].data() == buffer.getChannelPointer(channel));
    }
};

auto tSplitsFlatSpan = test("Buffer/splitsAFlatSpanEvenlyBetweenChannels") = []
{
    auto block = PlanarBlock {};
    auto buffer = Buffer {Span<float> {block.samples}, numChannels};

    check(buffer.getNumChannels() == numChannels);
    check(buffer.getNumSamples() == numSamples);

    // Same layout as the explicit-shape constructor.
    check(buffer.getChannel(2)[1] == 21.0f);
};

auto tSplitTruncates = test("Buffer/splittingAFlatSpanTruncatesTheRemainder") = []
{
    // 10 samples across 3 channels leaves a remainder: each channel gets 3 and
    // the odd sample is left out rather than over-running the block.
    auto samples = std::array<float, 10> {};
    auto buffer = Buffer {Span<float> {samples}, 3};

    check(buffer.getNumChannels() == 3);
    check(buffer.getNumSamples() == 3);
};

auto tWritesLand = test("Buffer/writesThroughAChannelLandInTheBlock") = []
{
    auto block = PlanarBlock {};
    auto buffer = block.view();

    // Channel is a contiguous range, so the standard vocabulary works on it.
    buffer.getChannel(1).fill(-1.0f);

    for (auto sample = 0; sample < numSamples; ++sample)
        check(block.samples[numSamples + sample] == -1.0f);

    // Neighbouring channels are untouched.
    check(block.samples[0] == 0.0f);
    check(block.samples[2 * numSamples] == 20.0f);
};

auto tIterates = test("Buffer/iteratesItsChannels") = []
{
    auto block = PlanarBlock {};

    auto seen = std::vector<float> {};

    for (auto channel: block.view())
        seen.push_back(channel[0]);

    check(seen.size() == numChannels);
    check(seen[0] == 0.0f);
    check(seen[1] == 10.0f);
    check(seen[2] == 20.0f);
};

auto tTemporarySafe = test("Buffer/channelsOutlivesTheTemporaryItCameFrom") = []
{
    auto block = PlanarBlock {};

    // The Buffer temporary dies before the loop body runs in C++20; channels()
    // carries the shape by value, so iterating it stays valid. This is the case
    // Buffer.h documents - if channels() ever went back to pointing at the
    // Buffer, this test is what catches it.
    auto total = 0.0f;

    for (auto channel: block.view().channels())
        total += channel[0];

    check(total == 30.0f);
};
} // namespace
