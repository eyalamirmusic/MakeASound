// What the audio thread and the MIDI input thread run must never reach the
// allocator: a malloc behind a lock the OS holds is how a callback misses its
// deadline. These cases pin that for the paths that have no hardware in them - the
// MIDI event/byte conversions, the block buffers, the planar views a callback sees,
// and the facade calls a host makes with nothing open. The live-thread half of the
// question is in RealtimeThreadAllocationTests.cpp.

#include "AllocationProbe.h"

#include <MakeASound/MakeASound.h>
#include <MakeASound/Common/Algorithms.h>

#include <NanoTest/NanoTest.h>

#include <array>
#include <cstdint>

using namespace nano;
using Probe::allocationsIn;

using MakeASound::AudioCallbackInfo;
using MakeASound::MidiBlockSync;
using MakeASound::MidiEvents;
using MakeASound::MidiInputEvent;
using MakeASound::MidiManager;
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
    auto* leaked = static_cast<int*>(nullptr);
    auto count = allocationsIn([&leaked] { leaked = new int {1}; });

    delete leaked;

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

    auto count = allocationsIn(
        [&] { error = midi.sendMessage(Event::noteOn(0, 60, 1.f)); });

    check(count == 0);
    check(error == MakeASound::Error::INVALID_USE);
};

auto tCallbackInfo = test("Allocations/audioCallbackInfoIsAllViewsAndInts") = []
{
    // What the facade does around every user callback: compare the shape against the
    // previous block, then hand out planar views over the backend's scratch.
    auto samples = std::array<float, 2 * 128> {};

    auto info = AudioCallbackInfo {};
    info.numOutputs = 2;
    info.outputBuffer = samples.data();
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

            for (auto channel: output.channels())
            {
                channel.fill(0.25f);
                written += channel[0];
            }
        });

    check(count == 0);
    check(changed);
    check(written == 0.5f);
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
} // namespace
