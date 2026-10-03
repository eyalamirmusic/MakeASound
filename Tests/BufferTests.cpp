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

float valueAt(int channel, int sample) noexcept
{ return static_cast<float>(channel * 10 + sample); }

auto tStridedConstructor = test("Buffer/stridedConstructorReportsItsStride") = []
{
    auto block = PlanarBlock {};
    auto buffer = Buffer {block.samples.data(), numChannels, 2, numSamples};

    check(buffer.getNumSamples() == 2);
    check(buffer.getChannelStride() == numSamples);
    check(!buffer.isContiguous());
    check(buffer.getChannelPointer(2) == block.samples.data() + 2 * numSamples);
    check(buffer[2][1] == valueAt(2, 1));
};

auto tContiguousByDefault = test("Buffer/defaultLayoutIsContiguous") = []
{
    auto block = PlanarBlock {};
    auto buffer = block.view();

    check(buffer.isContiguous());
    check(buffer.getChannelStride() == numSamples);
};

auto tSubBuffer = test("Buffer/subBufferOffsetsEveryChannel") = []
{
    auto block = PlanarBlock {};
    auto sub = block.view().subBuffer(1, 2);

    check(sub.getNumChannels() == numChannels);
    check(sub.getNumSamples() == 2);
    check(sub.getChannelStride() == numSamples);
    check(!sub.isContiguous());

    for (auto channel = 0; channel < numChannels; ++channel)
    {
        check(sub.getChannelPointer(channel)
              == block.samples.data() + channel * numSamples + 1);
        check(sub[channel][0] == valueAt(channel, 1));
        check(sub[channel][1] == valueAt(channel, 2));
    }
};

auto tSubBufferClamps = test("Buffer/subBufferClampsToItsSource") = []
{
    auto block = PlanarBlock {};

    check(block.view().subBuffer(3, 10).getNumSamples() == 1);
    check(block.view().subBuffer(numSamples, 1).isEmpty());
};

auto tSubBufferIterates = test("Buffer/subBufferChannelsHonourTheStride") = []
{
    auto block = PlanarBlock {};
    auto seen = std::vector<float> {};

    for (auto channel: block.view().subBuffer(2, 2).channels())
    {
        check(channel.size() == 2);
        seen.push_back(channel[0]);
    }

    check(seen.size() == numChannels);
    check(seen[0] == valueAt(0, 2));
    check(seen[1] == valueAt(1, 2));
    check(seen[2] == valueAt(2, 2));
};

auto tClearSubBuffer = test("Buffer/clearOnASubBufferTouchesOnlyItsRange") = []
{
    auto block = PlanarBlock {};
    block.view().subBuffer(1, 2).clear();

    for (auto channel = 0; channel < numChannels; ++channel)
    {
        for (auto sample = 0; sample < numSamples; ++sample)
        {
            auto inRange = sample == 1 || sample == 2;
            auto expected = inRange ? 0.0f : valueAt(channel, sample);
            check(block.samples[channel * numSamples + sample] == expected);
        }
    }
};

auto tFillSubBuffer = test("Buffer/fillOnASubBufferTouchesOnlyItsRange") = []
{
    auto block = PlanarBlock {};
    block.view().subBuffer(3, 1).fill(-1.0f);

    for (auto channel = 0; channel < numChannels; ++channel)
    {
        for (auto sample = 0; sample < numSamples; ++sample)
        {
            auto expected = sample == 3 ? -1.0f : valueAt(channel, sample);
            check(block.samples[channel * numSamples + sample] == expected);
        }
    }
};

auto tCopyIntoSubBuffer = test("Buffer/copyFromFillsASubBuffer") = []
{
    auto block = PlanarBlock {};
    auto source = std::array<float, numChannels * 2> {1, 2, 3, 4, 5, 6};

    block.view().subBuffer(1, 2).copyFrom(Buffer {source.data(), numChannels, 2});

    for (auto channel = 0; channel < numChannels; ++channel)
    {
        auto row = block.view()[channel];
        check(row[0] == valueAt(channel, 0));
        check(row[1] == source[channel * 2]);
        check(row[2] == source[channel * 2 + 1]);
        check(row[3] == valueAt(channel, 3));
    }
};

auto tCopyFromSubBuffer = test("Buffer/copyFromReadsASubBuffer") = []
{
    auto block = PlanarBlock {};
    auto target = std::array<float, numChannels * 2> {};

    Buffer {target.data(), numChannels, 2}.copyFrom(block.view().subBuffer(2, 2));

    for (auto channel = 0; channel < numChannels; ++channel)
    {
        check(target[channel * 2] == valueAt(channel, 2));
        check(target[channel * 2 + 1] == valueAt(channel, 3));
    }
};

auto tCopyMismatched = test("Buffer/copyFromStopsAtTheSmallerShape") = []
{
    // The source is one channel of two samples followed by sentinels: reading
    // past its end would pull a 99 into the target.
    auto source = std::array<float, 6> {1, 2, 99, 99, 99, 99};
    auto block = PlanarBlock {};

    block.view().copyFrom(Buffer {source.data(), 1, 2});

    check(block.samples[0] == 1.0f);
    check(block.samples[1] == 2.0f);
    check(block.samples[2] == valueAt(0, 2));
    check(block.samples[3] == valueAt(0, 3));

    for (auto channel = 1; channel < numChannels; ++channel)
        check(block.view()[channel][0] == valueAt(channel, 0));
};

auto tCopyLongerSource = test("Buffer/copyFromIgnoresExtraSourceSamples") = []
{
    auto block = PlanarBlock {};
    auto target = std::array<float, 5> {-1, -1, -1, -1, -1};

    // Two channels of two samples, three apart, fed from the 3x4 block.
    Buffer {target.data(), 2, 2, 3}.copyFrom(block.view());

    check(target[0] == valueAt(0, 0));
    check(target[1] == valueAt(0, 1));
    check(target[2] == -1.0f);
    check(target[3] == valueAt(1, 0));
    check(target[4] == valueAt(1, 1));
};

auto tAddIntoSubBuffer = test("Buffer/addFromMixesIntoASubBuffer") = []
{
    auto block = PlanarBlock {};
    auto source = std::array<float, 8> {1, 1, 1, 1, 1, 1, 99, 99};

    // Two channels of three samples into a two-sample range of three channels:
    // only two channels and two samples are shared.
    block.view().subBuffer(2, 2).addFrom(Buffer {source.data(), 2, 3});

    for (auto channel = 0; channel < numChannels; ++channel)
    {
        for (auto sample = 0; sample < numSamples; ++sample)
        {
            auto mixed = channel < 2 && sample >= 2;
            auto expected = valueAt(channel, sample) + (mixed ? 1.0f : 0.0f);
            check(block.samples[channel * numSamples + sample] == expected);
        }
    }
};

auto tAddFromSubBuffer = test("Buffer/addFromReadsASubBuffer") = []
{
    auto block = PlanarBlock {};
    auto target = std::array<float, 4> {1, 1, 1, 1};

    Buffer {target.data(), 2, 2}.addFrom(block.view().subBuffer(1, 3));

    check(target[0] == 1.0f + valueAt(0, 1));
    check(target[1] == 1.0f + valueAt(0, 2));
    check(target[2] == 1.0f + valueAt(1, 1));
    check(target[3] == 1.0f + valueAt(1, 2));
};
} // namespace
