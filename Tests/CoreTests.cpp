// MakeASoundCoreTests: an executable that links MakeASoundCore and nothing else of
// ours, so it is the check that the core stands on its own. A core TU that reached
// for a device symbol would fail this link, and Tests/CMakeLists.txt fails the
// configure if the core's link closure ever names a backend. The cases touch each
// part of the vocabulary once; the behaviour is pinned in MakeASoundTests.

#include <MakeASound/MakeASoundCore.h>
#include <MakeASound/Common/Algorithms.h>

#include <NanoTest/NanoTest.h>

#include <array>
#include <cstdint>

using namespace nano;

namespace
{
auto bufferCase = test("Core/Buffer") = []
{
    auto buffer = MakeASound::Buffer {2, 8};
    buffer.fill(0.5f);
    buffer.applyGain(2.0f);

    check(buffer.getNumChannels() == 2);
    check(buffer.getNumSamples() == 8);
    check(buffer.getChannelPointer(1)[7] == 1.0f);

    auto tail = buffer.getSubBuffer(4);
    tail.clear();

    check(buffer.getChannelPointer(0)[3] == 1.0f);
    check(buffer.getChannelPointer(0)[4] == 0.0f);
};

auto midiCase = test("Core/MIDI") = []
{
    auto events = MakeASound::MIDI::Buffer {};
    events.push_back(MakeASound::MIDI::Event::noteOff(0, 60, 0.0f, 5));
    events.push_back(MakeASound::MIDI::Event::noteOn(0, 64, 1.0f, 2));
    events.sortByOffset();

    check(events[0].isNoteOn());
    check(events[1].isNoteOff());

    auto bytes = MakeASound::MIDI::toBytes(events[0]);
    auto parsed = MakeASound::MIDI::convertMidi(bytes.begin(), bytes.size);

    check(parsed.has_value());
    check(parsed->asNoteOn() != nullptr);
    check(parsed->asNoteOn()->pitch == 64);
};

auto parserCase = test("Core/MidiParser") = []
{
    auto parser = MakeASound::MidiParser {};
    auto noteOn = std::array<std::uint8_t, 3> {0x90, 60, 100};
    auto bytes = MakeASound::Span<const std::uint8_t>(noteOn.data(), 3);
    auto count = 0;

    parser.feed(bytes,
                MakeASound::MidiTimePoint {},
                [&](const MakeASound::MidiMessageView&) { ++count; });

    check(count == 1);
};

// A processor run by hand over a context prepared for its layout, the way a host
// of either kind drives one.
struct Halve : MakeASound::Processor
{
    void prepare(const MakeASound::ProcessSpec& specToUse) override
    {
        spec = specToUse;
    }

    void process(MakeASound::ProcessContext& context) noexcept override
    {
        context.mainOutput().copyFrom(context.mainInput());
        context.mainOutput().applyGain(0.5f);
    }

    MakeASound::ProcessSpec spec;
};

auto processCase = test("Core/ProcessSpec") = []
{
    auto processor = Halve {};
    auto spec = MakeASound::ProcessSpec {48000.0, 16, processor.getBusLayout()};
    processor.prepare(spec);

    check(processor.spec == spec);
    check(spec.roundedSampleRate() == 48000);

    auto context = MakeASound::ProcessContext {};
    context.prepare(spec.layout);

    auto input = MakeASound::Buffer {2, spec.maxBlockSize};
    auto output = MakeASound::Buffer {2, spec.maxBlockSize};
    input.fill(1.0f);

    context.inputs[0].referTo(input.getChannelPointers(), 2, spec.maxBlockSize);
    context.outputs[0].referTo(output.getChannelPointers(), 2, spec.maxBlockSize);
    processor.process(context);

    check(output.getChannelPointer(1)[15] == 0.5f);
};

bool near(float a, float b)
{
    return a - b < 1e-5f && b - a < 1e-5f;
}

auto parameterChangesCase = test("Core/ParameterChanges") = []
{
    using MakeASound::ParameterChange;

    auto context = MakeASound::ProcessContext {};
    context.prepare({}, 16, 8);
    auto& changes = context.paramChanges;

    check(changes.empty());
    check(changes.capacity() >= 8);

    changes.add(ParameterChange {.index = 1, .sampleOffset = 20, .value = 0.9f});
    changes.add(ParameterChange {
        .index = 0, .sampleOffset = 4, .value = 1.f, .rampSamples = 8});
    changes.add(ParameterChange {.index = 1, .sampleOffset = 4, .value = 0.1f});
    changes.add(ParameterChange {
        .index = 0, .sampleOffset = 20, .value = 0.2f, .previousValue = 1.f});
    changes.sortByOffset();

    check(changes[0].index == 0 && changes[0].sampleOffset == 4);
    check(changes[1].index == 1 && changes[1].sampleOffset == 4);
    check(changes[2].index == 1 && changes[2].sampleOffset == 20);
    check(changes[3].index == 0 && changes[3].sampleOffset == 20);

    check(near(changes[0].valueAt(4), 0.f));
    check(near(changes[0].valueAt(8), 0.5f));
    check(near(changes[0].valueAt(12), 1.f));

    check(near(changes.valueAt(0, 0, 0.7f), 0.f));
    check(near(changes.valueAt(0, 6, 0.7f), 0.25f));
    check(near(changes.valueAt(0, 19, 0.7f), 1.f));
    check(near(changes.valueAt(0, 25, 0.7f), 0.2f));
    check(near(changes.valueAt(1, 10, 0.7f), 0.1f));
    check(near(changes.valueAt(2, 10, 0.7f), 0.7f));
};

// Sorted runs interleaved, as one queue per parameter arrives: the merge keeps
// each run's order within an offset, as the insertion sort does.
auto mergeSortCase = test("Core/stableMergeSort") = []
{
    using MakeASound::ParameterChange;

    auto changes = MakeASound::ParameterChanges {};
    auto expected = MakeASound::ParameterChanges {};
    auto scratch = MakeASound::ParameterChanges {};

    for (auto run = 0; run < 7; ++run)
        for (auto offset = 0; offset < 50; offset += 1 + run)
            changes.add(ParameterChange {.index = run, .sampleOffset = offset % 13});

    expected = changes;
    expected.sortByOffset();
    scratch.reserveAtLeast(changes.size());

    MakeASound::Algorithms::stableMergeSort(
        changes,
        scratch,
        [](const ParameterChange& a, const ParameterChange& b)
        { return a.sampleOffset < b.sampleOffset; });

    check(changes.size() == expected.size());

    auto same = true;

    for (auto i = 0; i < changes.size(); ++i)
        same = same && changes[i].index == expected[i].index
               && changes[i].sampleOffset == expected[i].sampleOffset;

    check(same);
};

auto spscPeekCase = test("Core/SPSCQueuePeek") = []
{
    auto queue = MakeASound::SPSCQueue<int, 2> {};
    auto value = -1;

    check(!queue.peek(value) && value == -1);
    check(queue.push(1) && queue.push(2));
    check(queue.peek(value) && value == 1);
    check(queue.peek(value) && value == 1);
    check(queue.pop(value) && value == 1);
    check(queue.peek(value) && value == 2);
    check(queue.push(3));
    check(queue.pop(value) && value == 2);
    check(queue.pop(value) && value == 3);
    check(!queue.peek(value) && value == 3);
};
} // namespace
