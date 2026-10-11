// What the audio thread and the MIDI input thread run must never reach the
// allocator: a malloc behind a lock the OS holds is how a callback misses its
// deadline. These cases pin that for the paths that have no hardware in them - the
// MIDI event/byte conversions, the block buffers, the planar views a callback sees,
// and the facade calls a host makes with nothing open. The live-thread half of the
// question is in RealtimeThreadAllocationTests.cpp.

#include "AllocationProbe.h"

#include <MakeASound/MakeASound.h>
#include <MakeASound/DSP/MakeASoundDSP.h>
#include <MakeASound/Common/Algorithms.h>
#include <MakeASound/MIDI/MidiParser.h>

#include <NanoTest/NanoTest.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <new>

using namespace nano;
using Probe::allocationsIn;

using MakeASound::AudioCallbackInfo;
using MakeASound::Buffer;
using MakeASound::BusLayout;
using MakeASound::DeviceManager;
using MakeASound::Engine;
using MakeASound::MidiBlockSync;
using MakeASound::MidiEvents;
using MakeASound::MidiInputEvent;
using MakeASound::MidiManager;
using MakeASound::MidiMessageView;
using MakeASound::MidiParser;
using MakeASound::MidiTimePoint;
using MakeASound::ProcessContext;
using MakeASound::Processor;
using MakeASound::ProcessSpec;
using MakeASound::Span;
using MakeASound::SPSCQueue;
using MakeASound::MIDI::Event;

// Tests live in an anonymous namespace: NanoTest registers a case by constructing a
// namespace-scope variable, so two files naming one the same way would otherwise
// collide at link time.
namespace
{
auto tProbeWorks = test("Allocations/theProbeItselfSeesTheHeap") = []
{
    // Without this the rest of the file could pass by watching nothing at all.
    //
    // The calls go through volatile pointers: a new/delete pair the optimiser can
    // see both ends of is one it may drop, and Clang does at -O2 - the Release
    // builds on CI were watching an allocation that never happened.
    volatile auto allocate = static_cast<void* (*)(std::size_t)>(::operator new);
    volatile auto release = static_cast<void (*)(void*)>(::operator delete);

    auto* leaked = static_cast<void*>(nullptr);
    auto count = allocationsIn([&] { leaked = allocate(sizeof(int)); });

    release(leaked);

    check(count > 0);
};

auto tEventFactories = test("Allocations/midiEventsAreBuiltOnTheStack") = []
{
    // Every factory, including the SysEx one that copies bytes: the payload is a
    // variant of fixed-size structs, so none of them owns anything.
    auto events = MakeASound::MIDI::Buffer {};
    events.reserve(16);

    auto sysExBytes = std::array<std::uint8_t, 4> {0xF0, 0x7E, 0x09, 0xF7};

    auto count = allocationsIn(
        [&]
        {
            events.clear();
            events.add(Event::noteOn(0, 60, 1.f));
            events.add(Event::noteOff(0, 60, 0.f, 32));
            events.add(Event::controlChange(1, 74, 0.5f));
            events.add(Event::pitchBend(2, -0.25f));
            events.add(Event::channelAftertouch(3, 0.75f));
            events.add(Event::polyAftertouch(4, 64, 0.5f));
            events.add(Event::programChange(5, 12));
            events.add(Event::sysEx(sysExBytes.data(), 4, 16));
        });

    check(count == 0);
    check(events.size() == 8);
};

auto tConvertMidi = test("Allocations/incomingMidiBytesDecodeWithoutTheHeap") = []
{
    // The decode every arriving message goes through, over each status the converter
    // knows plus the ones it rejects.
    auto statuses =
        std::array<std::uint8_t, 8> {0x80, 0x90, 0xA0, 0xB0, 0xC0, 0xD0, 0xE0, 0xF0};

    auto decoded = 0;

    auto count = allocationsIn(
        [&]
        {
            for (auto status: statuses)
            {
                for (auto channel = 0; channel < 16; ++channel)
                {
                    auto bytes = std::array<std::uint8_t, 3> {
                        static_cast<std::uint8_t>(status | channel), 64, 100};

                    if (MakeASound::MIDI::convertMidi(bytes.data(), 3))
                        ++decoded;
                }
            }
        });

    check(count == 0);

    // 0xF0 is the one status with no typed event, so seven of the eight decode.
    check(decoded == 7 * 16);
};

auto tToBytes = test("Allocations/outgoingMidiEventsEncodeWithoutTheHeap") = []
{
    auto sysExBytes = std::array<std::uint8_t, 4> {0xF0, 0x7E, 0x09, 0xF7};

    auto events = MakeASound::MIDI::Buffer {};
    events.reserve(8);
    events.add(Event::noteOn(0, 60, 1.f));
    events.add(Event::noteOff(0, 60, 0.f));
    events.add(Event::controlChange(1, 74, 0.5f));
    events.add(Event::pitchBend(2, -0.25f));
    events.add(Event::channelAftertouch(3, 0.75f));
    events.add(Event::polyAftertouch(4, 64, 0.5f));
    events.add(Event::programChange(5, 12));
    events.add(Event::sysEx(sysExBytes.data(), 4));

    auto bytesWritten = 0;

    auto count = allocationsIn(
        [&]
        {
            for (const auto& event: events)
                bytesWritten += MakeASound::MIDI::toBytes(event).size;
        });

    check(count == 0);
    check(bytesWritten > 0);
};

auto tAccessors = test("Allocations/midiEventAccessorsAreFree") = []
{
    auto event = Event::controlChange(3, 74, 0.5f);
    auto seen = 0;

    auto count = allocationsIn(
        [&]
        {
            if (event.isControlChange())
                ++seen;

            if (event.asControlChange() != nullptr)
                ++seen;

            event.visit([&](const auto&) { ++seen; });
        });

    check(count == 0);
    check(seen == 3);
};

auto tSortByOffset = test("Allocations/midiBuffersSortInPlace") = []
{
    // The insertion sort is the reason a block's events can be ordered inside the
    // callback rather than on the way in.
    auto events = MakeASound::MIDI::Buffer {};
    events.reserve(64);

    for (auto i = 0; i < 64; ++i)
        events.add(Event::noteOn(0, 60, 1.f, (64 - i) * 4));

    auto count = allocationsIn([&] { events.sortByOffset(); });

    check(count == 0);
    check(events[0].sampleOffset <= events[63].sampleOffset);
};

auto tAddFromRoom = test("Allocations/midiBufferMergesIntoRoomItAlreadyHas") = []
{
    auto source = MakeASound::MIDI::Buffer {};
    source.reserve(32);

    for (auto i = 0; i < 32; ++i)
        source.add(Event::noteOn(0, 60 + (i % 12), 1.f, i));

    auto destination = MakeASound::MIDI::Buffer {};
    destination.reserve(96);

    auto count = allocationsIn(
        [&]
        {
            destination.clear();
            destination.addFrom(source);
            destination.addFrom(source);
            destination.addFrom(source);
        });

    check(count == 0);
    check(destination.size() == 96);
};

auto tAddFromGrowsOnce =
    test("Allocations/midiBufferMergeGrowsOnceForTheResult") = []
{
    // A merge that outgrows the destination has to allocate - the room is not there.
    // What it must not do is get there by repeated push_back growth: addFrom
    // reserves for the merged size, so the whole thing costs one reallocation (the
    // new block plus the old one released) however far short it started.
    auto source = MakeASound::MIDI::Buffer {};
    source.reserve(1024);

    for (auto i = 0; i < 1024; ++i)
        source.add(Event::noteOn(0, 60, 1.f, i));

    auto destination = MakeASound::MIDI::Buffer {};
    destination.reserve(32);

    for (auto i = 0; i < 32; ++i)
        destination.add(Event::noteOff(0, 60, 0.f, i));

    auto count = allocationsIn([&] { destination.addFrom(source); });

    check(count <= 2);
    check(destination.size() == 1056);
};

// The parser is what every native backend will run on the platform's MIDI
// thread, so all four of its paths - short messages, dump assembly, the
// oversize drop and the stall timeout - are measured under the ban. The buffer
// and the parser are built outside it: the ban counts frees too.

auto tParserShortMessages =
    test("Allocations/theMidiParserDecodesShortMessagesOffTheHeap") = []
{
    auto sysExBuffer = std::array<std::uint8_t, 256> {};
    auto parser = MidiParser {Span<std::uint8_t>(sysExBuffer.data(), 256)};
    parser.setIgnoredTypes(false, false);

    // Running status, a two-byte message, system common, and a realtime byte in
    // the middle of a channel message: every branch except SysEx.
    auto voice =
        std::array<std::uint8_t, 7> {0x90, 0x3C, 0x64, 0x3E, 0x64, 0xC0, 0x01};

    auto system =
        std::array<std::uint8_t, 7> {0xF2, 0x10, 0x20, 0x90, 0x40, 0xF8, 0x64};

    auto delivered = 0;
    auto onMessage = [&delivered](const MidiMessageView&) { ++delivered; };

    auto count = allocationsIn(
        [&]
        {
            auto now = MidiTimePoint {};
            parser.feed(Span<const std::uint8_t>(voice.data(), 7), now, onMessage);
            parser.feed(Span<const std::uint8_t>(system.data(), 7), now, onMessage);
        });

    check(count == 0);
    check(delivered == 6);
};

auto tParserSysEx = test("Allocations/theMidiParserAssemblesSysExOffTheHeap") = []
{
    auto sysExBuffer = std::array<std::uint8_t, 1024> {};
    auto parser = MidiParser {Span<std::uint8_t>(sysExBuffer.data(), 1024)};

    auto opening = std::array<std::uint8_t, 1> {0xF0};
    auto closing = std::array<std::uint8_t, 1> {0xF7};
    auto chunk = std::array<std::uint8_t, 128> {};
    chunk.fill(0x01);

    auto delivered = 0;
    auto onMessage = [&delivered](const MidiMessageView& message)
    { delivered = message.bytes.size(); };

    auto count = allocationsIn(
        [&]
        {
            auto now = MidiTimePoint {};
            parser.feed(Span<const std::uint8_t>(opening.data(), 1), now, onMessage);

            for (auto i = 0; i < 6; ++i)
                parser.feed(
                    Span<const std::uint8_t>(chunk.data(), 128), now, onMessage);

            parser.feed(Span<const std::uint8_t>(closing.data(), 1), now, onMessage);
        });

    check(count == 0);
    check(delivered == 6 * 128 + 2);
};

auto tParserOversizeSysEx =
    test("Allocations/theMidiParserDropsAnOversizeSysExOffTheHeap") = []
{
    // The path a sample dump takes when the port's buffer cannot hold it: the
    // bytes are counted and thrown away, never accumulated.
    auto sysExBuffer = std::array<std::uint8_t, 16> {};
    auto parser = MidiParser {Span<std::uint8_t>(sysExBuffer.data(), 16)};

    auto dump = std::array<std::uint8_t, 512> {};
    dump.fill(0x02);
    dump.front() = 0xF0;
    dump.back() = 0xF7;

    auto delivered = 0;
    auto dropped = 0;
    auto onMessage = [&delivered](const MidiMessageView&) { ++delivered; };
    auto onDropped = [&dropped](int numBytes) { dropped = numBytes; };

    auto count = allocationsIn(
        [&]
        {
            parser.feed(Span<const std::uint8_t>(dump.data(), 512),
                        MidiTimePoint {},
                        onMessage,
                        onDropped);
        });

    check(count == 0);
    check(delivered == 0);
    check(dropped == 512);
};

auto tParserSysExTimeout =
    test("Allocations/theMidiParserAbandonsAStalledSysExOffTheHeap") = []
{
    auto sysExBuffer = std::array<std::uint8_t, 64> {};
    auto parser = MidiParser {Span<std::uint8_t>(sysExBuffer.data(), 64)};
    parser.setSysExTimeout(std::chrono::milliseconds {10});

    auto opening = std::array<std::uint8_t, 3> {0xF0, 0x43, 0x00};

    auto dropped = 0;
    auto onMessage = [](const MidiMessageView&) {};
    auto onDropped = [&dropped](int numBytes) { dropped = numBytes; };

    auto count = allocationsIn(
        [&]
        {
            auto start = MidiTimePoint {};

            parser.feed(Span<const std::uint8_t>(opening.data(), 3),
                        start,
                        onMessage,
                        onDropped);

            // An empty tick a second later is what the owner calls to give up.
            parser.feed(Span<const std::uint8_t> {},
                        start + std::chrono::seconds {1},
                        onMessage,
                        onDropped);
        });

    check(count == 0);
    check(dropped == 3);
};

auto tDrainNoPorts = test("Allocations/drainingWithNoInputOpenTouchesNothing") = []
{
    auto midi = MidiManager {};
    auto events = MidiEvents {};

    auto count = allocationsIn(
        [&]
        {
            events.clear();
            midi.drainMessages(events);
        });

    check(count == 0);
    check(events.empty());
};

auto tBlockSync = test("Allocations/midiBlockSyncStaysOffTheHeap") = []
{
    // Once per audio callback, so this is the one MIDI call the audio thread makes
    // unconditionally - including the first-block path that only stamps offsets.
    auto midi = MidiManager {};
    auto sync = MidiBlockSync {};

    auto count = allocationsIn(
        [&]
        {
            sync.drainForBlock(midi, 512, 48000);
            sync.drainForBlock(midi, 512, 48000);
            sync.reset();
        });

    check(count == 0);
    check(sync.empty());
};

auto tEventsWithinCapacity =
    test("Allocations/midiEventsFillTheCapacityTheyReserved") = []
{
    // MidiEvents reserves at construction precisely so the drain into it is free;
    // filling it to that capacity has to stay that way.
    auto events = MidiEvents {MidiEvents::defaultCapacity};

    auto count = allocationsIn(
        [&]
        {
            events.clear();

            for (auto i = 0; i < MidiEvents::defaultCapacity; ++i)
            {
                auto event = MidiInputEvent {};
                event.portId = 1;
                event.event = Event::noteOn(0, 60, 1.f, i);
                events.raw().add(event);
            }
        });

    check(count == 0);
    check(events.size() == MidiEvents::defaultCapacity);
};

auto tSendClosed = test("Allocations/sendingToAClosedOutputTouchesNothing") = []
{
    // The refusal path: a host that keeps sending while the cable is out must not
    // pay for it, and toBytes runs before the check.
    auto midi = MidiManager {};
    auto error = MakeASound::Error::NoError;

    auto count =
        allocationsIn([&] { error = midi.sendMessage(Event::noteOn(0, 60, 1.f)); });

    check(count == 0);
    check(error == MakeASound::Error::INVALID_USE);
};

auto tCallbackInfo = test("Allocations/audioCallbackInfoIsAllViewsAndInts") = []
{
    // What the facade does around every user callback: compare the shape against the
    // previous block, then hand out referring Buffers over the backend's scratch.
    auto samples = std::array<float, 2 * 128> {};
    float* table[] = {samples.data(), samples.data() + 128};

    auto info = AudioCallbackInfo {};
    info.numOutputs = 2;
    info.outputChannels = table;
    info.numSamples = 128;
    info.sampleRate = 48000;
    info.maxBlockSize = 128;

    auto previous = AudioCallbackInfo {};
    auto changed = false;
    auto written = 0.f;

    auto count = allocationsIn(
        [&]
        {
            changed = previous != info;
            previous = info;

            auto output = info.getOutput();

            for (auto channel: output)
            {
                channel.fill(0.25f);
                written += channel[0];
            }
        });

    check(count == 0);
    check(changed);
    check(written == 0.5f);
};

auto tBufferViews = test("Allocations/bufferSlicesAndOperationsStayOffTheHeap") = []
{
    // Everything a process callback does with a Buffer short of giving it
    // storage: refer, slice, subset, iterate, mix, move.
    auto owner = Buffer {4, 128};
    auto source = Buffer {4, 128};
    auto sum = 0.f;

    auto count = allocationsIn(
        [&]
        {
            auto referring = Buffer {owner.getChannelPointers(), 4, 128};
            auto tail = referring.getSubBuffer(64);
            auto pair = tail.getChannelSubset(2, 2);
            auto single = pair.getSingleChannel(1);

            pair.copyFrom(source);
            pair.addFrom(source, 0.5f);
            single.applyGain(2.f);
            tail.fill(0.25f);
            referring.getSubBuffer(0, 64).clear();

            for (auto channel: referring)
                sum += channel[64];

            auto moved = std::move(single);
            sum += moved[0][0];
        });

    check(count == 0);
    check(sum == 4 * 0.25f + 0.25f);
};

auto tBufferSetSizeReuses = test("Allocations/bufferSetSizeReusesItsCapacity") = []
{
    // The prepare-then-process pattern: size once at the largest shape, then every
    // equal-or-smaller setSize is free.
    auto buffer = Buffer {2, 512};

    auto count = allocationsIn(
        [&]
        {
            buffer.setSize(2, 256);
            buffer.setSize(1, 512);
            buffer.setSize(2, 512);
        });

    check(count == 0);
    check(buffer.getNumSamples() == 512);
};

auto tInsertionSort = test("Allocations/theBlockSortIsInPlace") = []
{
    auto values = MakeASound::Vector<int> {};
    values.reserve(64);

    for (auto i = 0; i < 64; ++i)
        values.add(64 - i);

    auto count =
        allocationsIn([&] { MakeASound::Algorithms::stableInsertionSort(values); });

    check(count == 0);
    check(values[0] == 1);
};

auto tSpscQueue = test("Allocations/theSpscQueueNeverGrows") = []
{
    // The queue a host uses to get parameter changes to the callback: bounded by
    // construction, so both ends stay off the heap.
    auto queue = SPSCQueue<int, 64> {};
    auto moved = 0;

    auto count = allocationsIn(
        [&]
        {
            for (auto i = 0; i < 200; ++i)
            {
                queue.push(i);

                auto out = 0;

                if (queue.pop(out))
                    ++moved;
            }
        });

    check(count == 0);
    check(moved == 200);
};

auto tProcessContext =
    test("Allocations/aPreparedProcessContextStaysOffTheHeap") = []
{
    // What a host does to the context every block, on a layout with every bus and
    // on one with none, where the accessors hand back the stand-ins.
    auto layout = BusLayout::instrument();
    layout.inputs.add({"Input", 2});
    layout.midiOutputs.add({"MIDI Out"});

    auto full = ProcessContext {};
    full.prepare(layout);

    auto bare = ProcessContext {};
    bare.prepare(BusLayout {});

    auto samples = std::array<float, 2 * 64> {};
    float* table[] = {samples.data(), samples.data() + 64};

    auto inputChannels = 0;
    auto outputChannels = 0;
    auto bareChannels = 0;

    auto count = allocationsIn(
        [&]
        {
            full.clearMidi();
            bare.clearMidi();

            for (auto i = 0; i < 300; ++i)
            {
                full.mainMidiIn().add(Event::noteOn(0, 60, 1.f, 300 - i));
                full.mainMidiOut().add(Event::noteOff(0, 60, 0.f, i));
                bare.mainMidiIn().add(Event::noteOn(0, 60, 1.f, 300 - i));
            }

            full.mainMidiIn().sortByOffset();
            bare.mainMidiIn().sortByOffset();

            full.inputs[0].referTo(table, 2, 64);
            full.outputs[0].referTo(table, 2, 64);
            full.mainOutput().fill(0.5f);

            inputChannels = full.mainInput().getNumChannels();
            outputChannels = full.mainOutput().getNumChannels();
            bareChannels = bare.mainInput().getNumChannels()
                           + bare.mainOutput().getNumChannels();
            bare.mainOutput().clear();
        });

    check(count == 0);
    check(inputChannels == 2);
    check(outputChannels == 2);
    check(bareChannels == 0);
    check(full.mainMidiIn().size() == 300);
    check(full.mainMidiIn()[0].sampleOffset == 1);
    check(samples[0] == 0.5f);
};

struct WritingProcessor : Processor
{
    BusLayout getBusLayout() const override { return BusLayout::instrument(); }

    void prepare(const ProcessSpec&) override {}

    void process(ProcessContext& context) noexcept override
    {
        for (auto channel: context.mainOutput())
            channel.fill(0.25f);

        midiEvents += context.mainMidiIn().size();
    }

    int midiEvents = 0;
};

auto tEngineSteadyState = test("Allocations/engineProcessStaysOffTheHeap") = []
{
    // The first block is dirty and may prepare; every one after it is the audio
    // thread's steady state, MidiBlockSync's drain included.
    auto devices = DeviceManager {};
    auto midi = MidiManager {};
    auto engine = Engine {devices, midi};
    auto processor = WritingProcessor {};

    auto samples = std::array<float, 3 * 256> {};
    float* table[] = {samples.data(), samples.data() + 256, samples.data() + 512};

    auto info = AudioCallbackInfo {};
    info.numOutputs = 3;
    info.outputChannels = table;
    info.numSamples = 256;
    info.sampleRate = 48000;
    info.maxBlockSize = 256;
    info.dirty = true;

    engine.prepare(processor, 48000, 256);
    engine.process(info);

    info.dirty = false;
    samples.fill(1.f);

    auto count = allocationsIn(
        [&]
        {
            engine.process(info);
            engine.process(info);
        });

    check(count == 0);
    check(samples[0] == 0.25f);
    check(samples[256 + 255] == 0.25f);
    check(samples[512] == 0.f);
    check(processor.midiEvents == 0);
};

auto tSmootherSteps = test("Allocations/smootherStepsOffTheHeap") = []
{
    auto smoother = MakeASound::Smoother();
    smoother.setSampleRate(48000);
    smoother.setRampTime(0.02f);
    smoother.setTarget(1.f);

    auto buffer = Buffer(2, 256);
    buffer.fill(1.f);

    auto count = allocationsIn(
        [&]
        {
            smoother.next();
            smoother.fill(buffer[0]);
            smoother.setTarget(0.5f);
            smoother.applyGain(buffer);
        });

    check(count == 0);
    check(smoother.isSmoothing());
};

auto tTestSynthBlock = test("Allocations/testSynthBlockStaysOffTheHeap") = []
{
    auto synth = MakeASound::DSP::TestSynth();
    auto spec = ProcessSpec {48000, 256, synth.getBusLayout()};
    synth.prepare(spec);

    auto output = Buffer(2, 256);
    auto context = ProcessContext();
    context.prepare(spec.layout);
    context.mainOutput().referTo(output.getChannelPointers(), 2, 256);

    auto& midi = context.mainMidiIn();
    midi.add(Event::noteOn(0, 60, 1.f, 0));
    midi.add(Event::noteOn(0, 64, 1.f, 64));
    midi.add(Event::noteOff(0, 60, 0.f, 128));
    midi.add(Event::controlChange(0, 123, 0.f, 192));

    auto settings = MakeASound::DSP::TestSynth::Settings {};
    settings.waveform = MakeASound::DSP::Waveform::Saw;
    settings.gain = 0.5f;

    auto count = allocationsIn(
        [&]
        {
            synth.setSettings(settings);
            synth.process(context);
            synth.reset();
        });

    check(count == 0);
};
} // namespace
