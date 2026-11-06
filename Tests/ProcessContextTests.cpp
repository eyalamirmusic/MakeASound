// ProcessContext's stand-ins for a bus the layout does not declare: what a
// process() written against the main*() accessors meets under a layout without
// that bus. (An undeclared audio input reading empty is pinned through the
// Engine, in Engine/aLayoutWithNoInputStillProcesses.)

#include <MakeASound/Audio/ProcessContext.h>

#include <NanoTest/NanoTest.h>

namespace ProcessContextTests
{
using namespace nano;
using MakeASound::BusLayout;
using MakeASound::ProcessContext;
using MakeASound::MIDI::Event;

// Tests live in an anonymous namespace: NanoTest registers a case by
// constructing a namespace-scope variable, so two files naming one the same way
// would otherwise collide at link time.
namespace
{
// An audio effect with no MIDI in either direction, whose process() still
// reaches the main MIDI accessors.
auto tUndeclaredMidi =
    test("ProcessContext/anUndeclaredMidiBusReadsEmptyAndSwallowsWrites") = []
{
    auto context = ProcessContext {};
    context.prepare(BusLayout::stereoInOut());

    check(context.mainMidiIn().empty());
    check(context.mainMidiOut().empty());

    context.mainMidiOut().add(Event::noteOn(0, 60, 0.8f, 0));

    check(context.mainMidiOut().size() == 1);
    check(context.mainMidiIn().empty(), "the out stand-in is not the in one");
    check(context.midiOut.empty(), "and nothing reaches a host");
};

auto tUndeclaredMidiClears =
    test("ProcessContext/clearMidiEmptiesTheMidiStandIns") = []
{
    auto context = ProcessContext {};
    context.prepare(BusLayout::stereoInOut());

    for (auto block = 0; block < 8; ++block)
    {
        context.clearMidi();
        context.mainMidiIn().add(Event::noteOn(0, 60, 0.8f, 0));
        context.mainMidiOut().add(Event::noteOn(0, 60, 0.8f, 0));
    }

    check(context.mainMidiIn().size() == 1);
    check(context.mainMidiOut().size() == 1);

    context.clearMidiOut();
    check(context.mainMidiOut().empty());
    check(context.mainMidiIn().size() == 1);
};
} // namespace
} // namespace ProcessContextTests
