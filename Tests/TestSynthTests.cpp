// Tests for DSP::TestSynth, the instrument the Synth app and the Synth plugin
// both play: what a block sounds like around its events, the note stack, and
// the envelope's edges.

#include <MakeASound/DSP/MakeASoundDSP.h>

#include <NanoTest/NanoTest.h>

#include <algorithm>
#include <cmath>

using namespace nano;

using MakeASound::Buffer;
using MakeASound::ProcessContext;
using MakeASound::ProcessSpec;
using MakeASound::DSP::TestSynth;
using MakeASound::DSP::Waveform;
using MakeASound::MIDI::Event;

// Tests live in an anonymous namespace: NanoTest registers a case by
// constructing a namespace-scope variable, so two files naming one the same way
// would otherwise collide at link time.
namespace
{
constexpr auto sampleRate = 48000;
constexpr auto blockSize = 256;

struct Rig
{
    Rig()
    {
        auto spec = ProcessSpec {sampleRate, blockSize, synth.getBusLayout()};
        context.prepare(spec.layout);
        context.mainOutput().referTo(output.getChannelPointers(), 2, blockSize);
        synth.prepare(spec);
    }

    void process()
    {
        output.clear();
        synth.process(context);
        context.clearMidi();
    }

    void settle()
    {
        for (auto i = 0; i < 20; ++i)
            process();
    }

    TestSynth synth;
    ProcessContext context;
    Buffer output {2, blockSize};
};

bool isSilent(const Buffer& buffer, int from = 0)
{
    for (auto channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (auto sample = from; sample < buffer.getNumSamples(); ++sample)
            if (buffer[channel][sample] != 0.f)
                return false;

    return true;
}

float peak(const Buffer& buffer)
{
    auto result = 0.f;

    for (auto channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (auto sample = 0; sample < buffer.getNumSamples(); ++sample)
            result = std::max(result, std::abs(buffer[channel][sample]));

    return result;
}

auto tSilentWithNoNote = test("TestSynth/silentUntilANoteArrives") = []
{
    auto rig = Rig {};
    rig.process();
    check(isSilent(rig.output));
    check(!rig.synth.isPlaying());
};

auto tNoteStartsAtItsOffset = test("TestSynth/soundStartsAtTheEventOffset") = []
{
    auto rig = Rig {};
    rig.context.mainMidiIn().add(Event::noteOn(0, 60, 1.f, 100));
    rig.process();

    check(rig.synth.getCurrentNote() == 60);
    check(rig.synth.getVelocity() == 1.f);

    for (auto sample = 0; sample < 100; ++sample)
        check(rig.output[0][sample] == 0.f);

    check(!isSilent(rig.output, 100));
};

auto tStereoCopies = test("TestSynth/everyChannelCarriesTheSameSignal") = []
{
    auto rig = Rig {};
    rig.context.mainMidiIn().add(Event::noteOn(0, 60, 1.f));
    rig.settle();

    for (auto sample = 0; sample < blockSize; ++sample)
        check(rig.output[0][sample] == rig.output[1][sample]);
};

auto tLevelFollowsGain = test("TestSynth/peakFollowsTheGainAndVelocity") = []
{
    auto rig = Rig {};
    auto settings = TestSynth::Settings {};
    settings.waveform = Waveform::Square;
    settings.gain = 0.5f;
    rig.synth.setSettings(settings);

    rig.context.mainMidiIn().add(Event::noteOn(0, 60, 0.5f));
    rig.settle();

    check(std::abs(peak(rig.output) - 0.25f) < 1e-4f);
};

auto tReleaseDecaysToSilence = test("TestSynth/noteOffReleasesToSilence") = []
{
    auto rig = Rig {};
    auto settings = TestSynth::Settings {};
    settings.releaseSeconds = 0.001f;
    rig.synth.setSettings(settings);

    rig.context.mainMidiIn().add(Event::noteOn(0, 60, 1.f));
    rig.settle();
    check(!isSilent(rig.output));

    rig.context.mainMidiIn().add(Event::noteOff(0, 60, 0.f));
    rig.process();
    check(!rig.synth.isPlaying());
    check(!isSilent(rig.output));

    rig.process();
    check(isSilent(rig.output));
};

auto tLastNotePriority = test("TestSynth/releaseFallsBackToTheNewestHeldNote") = []
{
    auto rig = Rig {};
    auto& midi = rig.context.mainMidiIn();

    midi.add(Event::noteOn(0, 60, 1.f, 0));
    midi.add(Event::noteOn(0, 64, 1.f, 10));
    midi.add(Event::noteOn(0, 67, 1.f, 20));
    rig.process();
    check(rig.synth.getCurrentNote() == 67);

    midi.add(Event::noteOff(0, 67, 0.f));
    rig.process();
    check(rig.synth.getCurrentNote() == 64);

    midi.add(Event::noteOff(0, 60, 0.f));
    rig.process();
    check(rig.synth.getCurrentNote() == 64);

    midi.add(Event::noteOff(0, 64, 0.f));
    rig.process();
    check(!rig.synth.isPlaying());
};

auto tAllNotesOff = test("TestSynth/cc123AndAllNotesOffClearTheStack") = []
{
    auto rig = Rig {};
    auto& midi = rig.context.mainMidiIn();

    midi.add(Event::noteOn(0, 60, 1.f));
    midi.add(Event::noteOn(0, 64, 1.f));
    midi.add(Event::controlChange(0, 123, 0.f));
    rig.process();
    check(!rig.synth.isPlaying());

    midi.add(Event::noteOn(0, 60, 1.f));
    rig.process();
    check(rig.synth.isPlaying());

    rig.synth.allNotesOff();
    check(!rig.synth.isPlaying());

    midi.add(Event::noteOff(0, 60, 0.f));
    rig.process();
    check(!rig.synth.isPlaying());
};

auto tLegatoKeepsTheEnvelope =
    test("TestSynth/legatoKeepsTheEnvelopeAcrossNotes") = []
{
    auto retrigger = [](bool legato)
    {
        auto rig = Rig {};
        auto settings = TestSynth::Settings {};
        settings.waveform = Waveform::Square;
        settings.attackSeconds = 1.f;
        settings.legato = legato;
        rig.synth.setSettings(settings);

        rig.context.mainMidiIn().add(Event::noteOn(0, 60, 1.f));
        rig.settle();

        rig.context.mainMidiIn().add(Event::noteOn(0, 64, 1.f, 0));
        rig.process();
        return std::abs(rig.output[0][0]);
    };

    check(retrigger(true) > 0.02f);
    check(retrigger(false) < 1e-4f);
};

auto tResetSilences = test("TestSynth/resetDropsTheNoteAndTheEnvelope") = []
{
    auto rig = Rig {};
    rig.context.mainMidiIn().add(Event::noteOn(0, 60, 1.f));
    rig.settle();

    rig.synth.reset();
    check(!rig.synth.isPlaying());

    rig.process();
    check(isSilent(rig.output));
};

auto tFrequency = test("TestSynth/noteToFrequencyIsEqualTemperament") = []
{
    check(TestSynth::noteToFrequency(69) == 440.f);
    check(std::abs(TestSynth::noteToFrequency(81) - 880.f) < 1e-3f);
    check(std::abs(TestSynth::noteToFrequency(57) - 220.f) < 1e-3f);
};

} // namespace
