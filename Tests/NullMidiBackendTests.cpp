// The null MIDI backend on its own, built on every platform: no ports, every open
// refused with the error a caller already handles, and nothing that crashes or
// blocks. It is what Android and Emscripten get, and what any platform gets with
// MAKEASOUND_MIDI_BACKEND=Null; NullMidiFacadeTests covers that build through the
// façade.

#include <MakeASound/NullMIDI/NullMidiManager.h>

#include <NanoTest/NanoTest.h>

#include <array>
#include <cstdint>

namespace NullMidiBackendTests
{
using namespace nano;

using MakeASound::Error;
using MakeASound::MidiInputEvent;
using MakeASound::Vector;

using Backend = MakeASound::NullMIDI::MidiManager;

namespace
{
auto tNothing = test("NullMidi/hasNoMidiSystemAndNoPorts") = []
{
    auto midi = Backend {};

    check(!midi.isAvailable());
    check(midi.getLastError() == Error::NoError);
    check(midi.getInputPorts().empty());
    check(midi.getOutputPorts().empty());
};

auto tInputs = test("NullMidi/refusesEveryInput") = []
{
    auto midi = Backend {};

    check(midi.openInput(0, {}) == Error::SYSTEM_ERROR);
    check(midi.getLastError() == Error::SYSTEM_ERROR);

    // A virtual port's id is a caller's mistake, named as every backend names it.
    check(midi.openInput(-1, {}) == Error::INVALID_PARAMETER);

    check(!midi.openVirtualInput("MakeASound Null", {}).has_value());
    check(midi.getLastError() == Error::SYSTEM_ERROR);

    check(!midi.isInputOpen(0));
    check(midi.getOpenInputPorts().empty());

    midi.closeInput(0);
    midi.closeAllInputs();
};

auto tOutputs = test("NullMidi/refusesEveryOutput") = []
{
    auto midi = Backend {};

    check(midi.openOutput(0) == Error::SYSTEM_ERROR);
    check(midi.openVirtualOutput("MakeASound Null") == Error::SYSTEM_ERROR);
    check(!midi.isOutputOpen());

    // Sending with no output open is what every backend reports it as.
    auto noteOn = std::array<std::uint8_t, 3> {0x90, 60, 100};
    check(midi.sendMessage(noteOn.data(), noteOn.size()) == Error::INVALID_USE);

    midi.closeOutput();
    check(!midi.isOutputOpen());
};

auto tDrain = test("NullMidi/drainsNothingAndRaisesNothing") = []
{
    auto midi = Backend {};
    auto raised = 0;

    midi.setNotificationCallback([&](auto) { ++raised; });
    midi.setIgnoredTypes(false, false);
    midi.setMaxSysExBytes(1024);
    check(midi.getMaxSysExBytes() == 1024);

    auto events = Vector<MidiInputEvent> {};
    events.reserve(16);
    midi.drainMessages(events);

    check(events.empty());
    check(midi.takeNotifications().empty());
    check(raised == 0);
};
} // namespace
} // namespace NullMidiBackendTests
