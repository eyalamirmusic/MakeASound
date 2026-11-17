// MidiManager over the null backend, built only when the device library links it
// (MAKEASOUND_MIDI_BACKEND=Null, or a platform with no MIDI API of ours; see
// Tests/CMakeLists.txt). The façade has to stay constructible and quiet: an
// audio-only app on Android makes one exactly as a desktop app does.

#include <MakeASound/MakeASound.h>

#include <NanoTest/NanoTest.h>

namespace NullMidiFacadeTests
{
using namespace nano;

using MakeASound::Error;
using MakeASound::MidiEvents;
using MakeASound::MidiManager;
using MakeASound::MidiMessage;

namespace
{
auto tUnavailable = test("NullMidi/theFacadeReportsNoMidiSystem") = []
{
    auto midi = MidiManager {};

    check(!midi.isAvailable());
    check(midi.getInputPorts().empty());
    check(midi.getOutputPorts().empty());
};

auto tOpens = test("NullMidi/theFacadeRefusesEveryOpen") = []
{
    auto midi = MidiManager {};

    check(midi.openInput(0) == Error::SYSTEM_ERROR);
    check(midi.openInput(0, [](const MidiMessage&) {}) == Error::SYSTEM_ERROR);
    check(!midi.openVirtualInput("MakeASound Null").has_value());
    check(midi.getOpenInputPorts().empty());

    check(midi.openOutput(0) == Error::SYSTEM_ERROR);
    check(midi.openVirtualOutput("MakeASound Null") == Error::SYSTEM_ERROR);
    check(!midi.isOutputOpen());
    check(midi.getLastError() == Error::SYSTEM_ERROR);
};

auto tDrain = test("NullMidi/theFacadeDrainsNothing") = []
{
    auto midi = MidiManager {};
    auto events = MidiEvents {};

    midi.drainMessages(events);
    check(events.empty());
    check(midi.drainNotifications().empty());

    midi.closeAllInputs();
    midi.closeOutput();
};
} // namespace
} // namespace NullMidiFacadeTests
