// Tests for MakeASound::Buffer, the one planar block type: owning or referring,
// move-only, every slice another Buffer over the same channel table. What is
// worth pinning is the arithmetic it does on the caller's behalf - where each
// channel starts, that an offset lands writes in the right place, that slices
// compose - plus the ownership rules: what a move leaves behind, that copyOf is
// independent, that setSize keeps its capacity, and that the shape survives being
// read off a temporary (the C++20 lifetime case Buffer.h calls out).

#include <MakeASound/Audio/Buffer.h>

#include <NanoTest/NanoTest.h>

#include <array>
#include <type_traits>

using namespace nano;
using MakeASound::Buffer;
using MakeASound::Channel;
using MakeASound::ConstChannel;

// Tests live in an anonymous namespace: NanoTest registers a case by
// constructing a namespace-scope variable, so two files naming one the same way
// would otherwise collide at link time.
namespace
{
constexpr auto numChannels = 3;
constexpr auto numSamples = 4;

static_assert(!std::is_copy_constructible_v<Buffer>);
static_assert(!std::is_copy_assignable_v<Buffer>);
static_assert(std::is_nothrow_move_constructible_v<Buffer>);
static_assert(std::is_nothrow_move_assignable_v<Buffer>);

// A 3x4 block where channel c, sample s holds c * 10 + s, so any mix-up of
// channel/sample indexing shows up as an obviously wrong number. The storage is
// three separate arrays with a table over them - the layout a host hands over.
struct HostBlock
{
    HostBlock() noexcept
    {
        for (auto channel = 0; channel < numChannels; ++channel)
        {
            for (auto sample = 0; sample < numSamples; ++sample)
                storage[channel][sample] = static_cast<float>(channel * 10 + sample);

            table[channel] = storage[channel].data();
        }
    }

    Buffer view() noexcept { return {table, numChannels, numSamples}; }

    std::array<std::array<float, numSamples>, numChannels> storage {};
    float* table[numChannels] {};
};

Buffer makeOwned()
{
    auto buffer = Buffer {numChannels, numSamples};

    for (auto channel = 0; channel < numChannels; ++channel)
        for (auto sample = 0; sample < numSamples; ++sample)
            buffer[channel][sample] = static_cast<float>(channel * 10 + sample);

    return buffer;
}

// ---------------------------------------------------------------------------
// Shape and ownership
// ---------------------------------------------------------------------------

auto tDefaultEmpty = test("Buffer/defaultConstructedIsEmptyAndOwnsNothing") = []
{
    auto buffer = Buffer {};

    check(buffer.isEmpty());
    check(!buffer.isOwning());
    check(buffer.getNumChannels() == 0);
    check(buffer.getNumSamples() == 0);
    check(buffer.begin() == buffer.end());
};

auto tOwningShape = test("Buffer/owningConstructorAllocatesZeroedChannels") = []
{
    auto buffer = Buffer {numChannels, numSamples};

    check(buffer.isOwning());
    check(!buffer.isEmpty());
    check(buffer.getNumChannels() == numChannels);
    check(buffer.getNumSamples() == numSamples);
    check(buffer.getStartSample() == 0);

    for (auto channel: buffer)
        for (auto sample: channel)
            check(sample == 0.0f);
};

auto tOwningChannelsDistinct =
    test("Buffer/owningChannelsAreDistinctAndAligned") = []
{
    auto buffer = Buffer {numChannels, 5};

    for (auto channel = 1; channel < numChannels; ++channel)
    {
        auto distance = buffer.getChannelPointer(channel)
                        - buffer.getChannelPointer(channel - 1);

        check(distance >= 5);
        check(distance % 16 == 0);
    }
};

auto tReferringShape = test("Buffer/referringConstructorPointsAtTheTable") = []
{
    auto block = HostBlock {};
    auto buffer = block.view();

    check(!buffer.isOwning());
    check(buffer.getNumChannels() == numChannels);
    check(buffer.getNumSamples() == numSamples);
    check(buffer.getChannelPointers() == block.table);

    for (auto channel = 0; channel < numChannels; ++channel)
        check(buffer.getChannelPointer(channel) == block.storage[channel].data());
};

auto tReferringOffset = test("Buffer/referringConstructorAppliesTheStartSample") = []
{
    auto block = HostBlock {};
    auto buffer = Buffer {block.table, numChannels, 2, 1};

    check(buffer.getStartSample() == 1);
    check(buffer.getNumSamples() == 2);

    for (auto channel = 0; channel < numChannels; ++channel)
    {
        check(buffer.getChannelPointer(channel)
              == block.storage[channel].data() + 1);
        check(buffer[channel][0] == static_cast<float>(channel * 10 + 1));
    }
};

auto tAccessorsAgree = test("Buffer/getChannelAndSubscriptAgree") = []
{
    auto block = HostBlock {};
    auto buffer = block.view();

    for (auto channel = 0; channel < numChannels; ++channel)
    {
        check(buffer.getChannel(channel).data() == buffer[channel].data());
        check(buffer.getChannel(channel).data()
              == buffer.getChannelPointer(channel));
        check(buffer.getChannel(channel).size() == numSamples);
    }
};

auto tWritesLand = test("Buffer/writesThroughAChannelLandInTheSource") = []
{
    auto block = HostBlock {};
    auto buffer = block.view();

    buffer[1][2] = 99.0f;
    buffer.getChannel(2)[0] = 77.0f;

    check(block.storage[1][2] == 99.0f);
    check(block.storage[2][0] == 77.0f);
    check(block.storage[0][0] == 0.0f);
};

auto tConstHandsOutConstChannels =
    test("Buffer/constBufferHandsOutConstChannels") = []
{
    auto block = HostBlock {};
    const auto buffer = block.view();

    static_assert(std::is_same_v<decltype(buffer[0]), ConstChannel>);
    static_assert(std::is_same_v<decltype(*buffer.begin()), ConstChannel>);

    check(buffer[2][3] == 23.0f);
};

// ---------------------------------------------------------------------------
// Slicing
// ---------------------------------------------------------------------------

auto tSubBuffer = test("Buffer/subBufferOffsetsEveryChannel") = []
{
    auto block = HostBlock {};
    auto sub = block.view().getSubBuffer(1, 2);

    check(sub.getNumChannels() == numChannels);
    check(sub.getNumSamples() == 2);
    check(sub.getStartSample() == 1);
    check(!sub.isOwning());

    for (auto channel = 0; channel < numChannels; ++channel)
    {
        check(sub.getChannelPointer(channel) == block.storage[channel].data() + 1);
        check(sub[channel][0] == static_cast<float>(channel * 10 + 1));
        check(sub[channel][1] == static_cast<float>(channel * 10 + 2));
    }
};

auto tSubBufferToEnd = test("Buffer/subBufferWithoutALengthRunsToTheEnd") = []
{
    auto block = HostBlock {};
    auto sub = block.view().getSubBuffer(3);

    check(sub.getNumSamples() == 1);
    check(sub[1][0] == 13.0f);
};

auto tSubBufferClamps = test("Buffer/subBufferClampsToItsSource") = []
{
    auto block = HostBlock {};
    auto buffer = block.view();

    check(buffer.getSubBuffer(2, 10).getNumSamples() == 2);
    check(buffer.getSubBuffer(10, 2).getNumSamples() == 0);
    check(buffer.getSubBuffer(-3, 2).getStartSample() == 0);
    check(buffer.getSubBuffer(1, -1).getNumSamples() == 0);
};

auto tSubBuffersCompose = test("Buffer/subBuffersOfSubBuffersAddTheirOffsets") = []
{
    auto block = HostBlock {};
    auto inner = block.view().getSubBuffer(1, 3).getSubBuffer(1, 1);

    check(inner.getStartSample() == 2);
    check(inner.getNumSamples() == 1);
    check(inner[2][0] == 22.0f);
};

auto tChannelSubset = test("Buffer/channelSubsetSkipsTheFirstChannels") = []
{
    auto block = HostBlock {};
    auto subset = block.view().getChannelSubset(1, 2);

    check(subset.getNumChannels() == 2);
    check(subset.getNumSamples() == numSamples);
    check(subset.getChannelPointers() == block.table + 1);
    check(subset[0][0] == 10.0f);
    check(subset[1][0] == 20.0f);
};

auto tChannelSubsetClamps = test("Buffer/channelSubsetClampsToItsSource") = []
{
    auto block = HostBlock {};
    auto buffer = block.view();

    check(buffer.getChannelSubset(2, 5).getNumChannels() == 1);
    check(buffer.getChannelSubset(5, 1).getNumChannels() == 0);
    check(buffer.getChannelSubset(-1, 2).getNumChannels() == 2);
};

auto tSingleChannel = test("Buffer/singleChannelIsAOneChannelSubset") = []
{
    auto block = HostBlock {};
    auto single = block.view().getSingleChannel(2).getSubBuffer(1, 2);

    check(single.getNumChannels() == 1);
    check(single[0][0] == 21.0f);
    check(single[0][1] == 22.0f);
};

// ---------------------------------------------------------------------------
// Sample operations
// ---------------------------------------------------------------------------

auto tClearSubBuffer = test("Buffer/clearOnASubBufferTouchesOnlyItsRange") = []
{
    auto block = HostBlock {};
    block.view().getSubBuffer(1, 2).clear();

    for (auto channel = 0; channel < numChannels; ++channel)
    {
        check(block.storage[channel][0] == static_cast<float>(channel * 10));
        check(block.storage[channel][1] == 0.0f);
        check(block.storage[channel][2] == 0.0f);
        check(block.storage[channel][3] == static_cast<float>(channel * 10 + 3));
    }
};

auto tFillSubset = test("Buffer/fillOnAChannelSubsetTouchesOnlyItsChannels") = []
{
    auto block = HostBlock {};
    block.view().getChannelSubset(1, 1).fill(5.0f);

    check(block.storage[0][0] == 0.0f);
    check(block.storage[1][0] == 5.0f);
    check(block.storage[1][3] == 5.0f);
    check(block.storage[2][0] == 20.0f);
};

auto tCopyIntoSubBuffer = test("Buffer/copyFromFillsASubBuffer") = []
{
    auto block = HostBlock {};
    auto source = Buffer {numChannels, 2};
    source.fill(-1.0f);

    block.view().getSubBuffer(1, 2).copyFrom(source);

    for (auto channel = 0; channel < numChannels; ++channel)
    {
        check(block.storage[channel][0] == static_cast<float>(channel * 10));
        check(block.storage[channel][1] == -1.0f);
        check(block.storage[channel][2] == -1.0f);
        check(block.storage[channel][3] == static_cast<float>(channel * 10 + 3));
    }
};

auto tCopyMismatched = test("Buffer/copyFromStopsAtTheSmallerShape") = []
{
    auto block = HostBlock {};
    auto source = Buffer {1, 2};
    source.fill(-1.0f);

    block.view().copyFrom(source);

    check(block.storage[0][0] == -1.0f);
    check(block.storage[0][1] == -1.0f);
    check(block.storage[0][2] == 2.0f);
    check(block.storage[1][0] == 10.0f);
};

auto tAddFromWithGain = test("Buffer/addFromMixesWithAGain") = []
{
    auto block = HostBlock {};
    auto source = Buffer {numChannels, numSamples};
    source.fill(1.0f);

    block.view().getSubBuffer(2, 2).addFrom(source, 0.5f);

    check(block.storage[1][1] == 11.0f);
    check(block.storage[1][2] == 12.5f);
    check(block.storage[1][3] == 13.5f);
};

auto tApplyGain = test("Buffer/applyGainScalesEverySample") = []
{
    auto block = HostBlock {};
    block.view().getChannelSubset(1, 1).applyGain(2.0f);

    check(block.storage[0][1] == 1.0f);
    check(block.storage[1][1] == 22.0f);
    check(block.storage[2][1] == 21.0f);
};

// ---------------------------------------------------------------------------
// Iteration
// ---------------------------------------------------------------------------

auto tIterates = test("Buffer/iteratesItsChannelsInOrder") = []
{
    auto block = HostBlock {};
    auto buffer = block.view();
    auto index = 0;

    for (auto channel: buffer)
    {
        check(channel.data() == block.storage[index].data());
        check(channel.size() == numSamples);
        ++index;
    }

    check(index == numChannels);
};

auto tTemporarySafe = test("Buffer/channelsOutliveTheTemporaryTheyCameFrom") = []
{
    // The Buffer temporary from view() is what the range-for binds; the iterator
    // keeps the shape by value, so nothing points back at a dead Buffer.
    auto block = HostBlock {};
    auto sum = 0.0f;

    for (auto channel: block.view().getSubBuffer(1, 2))
        for (auto sample: channel)
            sum += sample;

    check(sum == (1 + 2) + (11 + 12) + (21 + 22));
};

// ---------------------------------------------------------------------------
// Moves, copies and resizing
// ---------------------------------------------------------------------------

auto tMoveOwning = test("Buffer/movingAnOwningBufferCarriesItsSamples") = []
{
    auto source = makeOwned();
    auto* firstChannel = source.getChannelPointer(0);

    auto moved = std::move(source);

    check(moved.isOwning());
    check(moved.getChannelPointer(0) == firstChannel);
    check(moved[2][3] == 23.0f);

    check(source.isEmpty());
    check(!source.isOwning());
};

auto tMoveAssignReleases = test("Buffer/moveAssigningOverAnOwnerReleasesIt") = []
{
    auto block = HostBlock {};
    auto target = makeOwned();

    target = block.view();

    check(!target.isOwning());
    check(target.getChannelPointers() == block.table);
    check(target[1][1] == 11.0f);
};

auto tMoveReferring = test("Buffer/movingAReferringBufferCopiesTheView") = []
{
    auto block = HostBlock {};
    auto source = block.view().getSubBuffer(1, 2);
    auto moved = std::move(source);

    check(!moved.isOwning());
    check(moved.getStartSample() == 1);
    check(moved.getNumSamples() == 2);
    check(moved[0][0] == 1.0f);
};

auto tCopyOf = test("Buffer/copyOfIsAnIndependentOwner") = []
{
    auto block = HostBlock {};
    auto copy = Buffer::copyOf(block.view());

    check(copy.isOwning());
    check(copy.getNumChannels() == numChannels);
    check(copy.getNumSamples() == numSamples);
    check(copy[2][3] == 23.0f);

    copy[0][0] = 99.0f;
    check(block.storage[0][0] == 0.0f);
};

auto tSetSizeZeroes = test("Buffer/setSizeZeroesAndReshapes") = []
{
    auto buffer = makeOwned();
    buffer.setSize(2, 8);

    check(buffer.getNumChannels() == 2);
    check(buffer.getNumSamples() == 8);

    for (auto channel: buffer)
        for (auto sample: channel)
            check(sample == 0.0f);
};

auto tSetSizeKeepsCapacity =
    test("Buffer/setSizeToASmallerShapeKeepsItsStorage") = []
{
    auto buffer = Buffer {2, 512};
    auto* storage = buffer.getChannelPointer(0);

    buffer.setSize(2, 256);
    check(buffer.getChannelPointer(0) == storage);

    buffer.setSize(1, 512);
    check(buffer.getChannelPointer(0) == storage);

    buffer.setSize(2, 512);
    check(buffer.getChannelPointer(0) == storage);
};

auto tSetSizeOnReferring = test("Buffer/setSizeOnAReferringBufferMakesItOwning") = []
{
    auto block = HostBlock {};
    auto buffer = block.view();

    buffer.setSize(1, 2);

    check(buffer.isOwning());
    check(buffer.getChannelPointers() != block.table);
    check(buffer[0][0] == 0.0f);
    check(block.storage[0][0] == 0.0f);
};

auto tReferToOnOwning = test("Buffer/referToOnAnOwningBufferDropsItsStorage") = []
{
    auto block = HostBlock {};
    auto buffer = makeOwned();

    buffer.referTo(block.table, numChannels, 2, 2);

    check(!buffer.isOwning());
    check(buffer.getStartSample() == 2);
    check(buffer[1][0] == 12.0f);
};
} // namespace
