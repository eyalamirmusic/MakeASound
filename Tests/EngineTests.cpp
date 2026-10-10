// Tests for MakeASound::Engine driven the way a device would drive it, but with
// no device: a fake processor, a synthetic AudioCallbackInfo over tables the case
// owns, and process() called directly. What is pinned is the contract in
// Engine.h - prepare-on-dirty, how the main buses bind to whatever channels the
// device has, what gets cleared, and that MIDI from the manager lands in the
// block it belongs to.

#include <MakeASound/MakeASound.h>

#include <NanoTest/NanoTest.h>

#include <chrono>
#include <functional>
#include <string>
#include <thread>
#include <vector>

using namespace nano;
using namespace std::chrono_literals;

using MakeASound::AudioCallbackInfo;
using MakeASound::BusLayout;
using MakeASound::DeviceManager;
using MakeASound::Engine;
using MakeASound::Error;
using MakeASound::MidiManager;
using MakeASound::ProcessContext;
using MakeASound::Processor;
using MakeASound::ProcessSpec;
using MakeASound::MIDI::Event;

// Tests live in an anonymous namespace: NanoTest registers a case by
// constructing a namespace-scope variable, so two files naming one the same way
// would otherwise collide at link time.
namespace
{
constexpr auto sampleRate = 48000;
constexpr auto blockSize = 256;
constexpr auto garbage = 7.f;

float patternAt(int channel, int sample) noexcept
{ return static_cast<float>(channel * 1000 + sample + 1); }

struct FakeProcessor : Processor
{
    BusLayout getBusLayout() const override { return layout; }

    void prepare(const ProcessSpec& spec) override
    {
        specs.push_back(spec);
        midiSeen.reserveAtLeast(ProcessContext::defaultMidiCapacity);
    }

    void reset() noexcept override { ++resets; }

    void process(ProcessContext& context) noexcept override
    {
        ++processed;
        midiOutWasEmpty = context.mainMidiOut().empty();

        midiSeen.clear();

        for (const auto& event: context.mainMidiIn())
            midiSeen.add(event);

        if (writeMidiOut)
            context.mainMidiOut().add(Event::noteOn(0, 60, 1.f));

        if (writePattern)
        {
            auto& output = context.mainOutput();

            for (auto ch = 0; ch < output.getNumChannels(); ++ch)
                for (auto s = 0; s < output.getNumSamples(); ++s)
                    output[ch][s] = patternAt(ch, s);
        }

        if (onProcess)
            onProcess(context);
    }

    BusLayout layout = BusLayout::stereoInOut();
    bool writePattern = false;
    bool writeMidiOut = false;
    std::function<void(ProcessContext&)> onProcess;

    std::vector<ProcessSpec> specs;
    int resets = 0;
    int processed = 0;
    bool midiOutWasEmpty = false;
    MakeASound::MIDI::Buffer midiSeen;
};

// Owning per-channel arrays plus the tables a backend would hand over. Output
// channels start full of garbage so a cleared channel is visibly cleared.
struct FakeStream
{
    FakeStream(int numInputsToUse, int numOutputsToUse, int capacity = 1024)
        : inputs(numInputsToUse, std::vector<float>(capacity))
        , outputs(numOutputsToUse, std::vector<float>(capacity, garbage))
    {
        for (auto& channel: inputs)
            inputTable.push_back(channel.data());

        for (auto& channel: outputs)
            outputTable.push_back(channel.data());
    }

    AudioCallbackInfo info(int numSamples = blockSize,
                           bool dirty = false,
                           int rate = sampleRate,
                           int maxBlockSize = blockSize)
    {
        auto result = AudioCallbackInfo {};
        result.numInputs = static_cast<int>(inputs.size());
        result.numOutputs = static_cast<int>(outputs.size());
        result.inputChannels = inputTable.empty() ? nullptr : inputTable.data();
        result.outputChannels = outputTable.empty() ? nullptr : outputTable.data();
        result.numSamples = numSamples;
        result.sampleRate = rate;
        result.maxBlockSize = maxBlockSize;
        result.dirty = dirty;
        return result;
    }

    // One dirty block at the prepared shape, as a freshly opened stream gives.
    void run(Engine& engine, int numSamples = blockSize, bool dirty = false)
    {
        auto block = info(numSamples, dirty);
        engine.process(block);
    }

    void fillInputs()
    {
        for (auto ch = 0; ch < static_cast<int>(inputs.size()); ++ch)
            for (auto s = 0; s < static_cast<int>(inputs[ch].size()); ++s)
                inputs[ch][s] = patternAt(ch + 10, s);
    }

    bool outputIs(int channel, float value, int numSamples = blockSize) const
    {
        for (auto s = 0; s < numSamples; ++s)
            if (outputs[channel][s] != value)
                return false;

        return true;
    }

    bool outputHoldsPattern(int deviceChannel,
                            int busChannel,
                            int numSamples = blockSize) const
    {
        for (auto s = 0; s < numSamples; ++s)
            if (outputs[deviceChannel][s] != patternAt(busChannel, s))
                return false;

        return true;
    }

    std::vector<std::vector<float>> inputs;
    std::vector<std::vector<float>> outputs;
    std::vector<float*> inputTable;
    std::vector<float*> outputTable;
};

// What a case needs to stand an Engine up: real managers, no stream opened.
struct Rig
{
    DeviceManager devices;
    MidiManager midi;
    Engine engine {devices, midi};
};

BusLayout layoutWith(int inputChannels, int outputChannels)
{
    auto layout = BusLayout {};

    if (inputChannels > 0)
        layout.inputs.add({"Input", inputChannels});

    if (outputChannels > 0)
        layout.outputs.add({"Output", outputChannels});

    return layout;
}

auto tPrepare = test("Engine/prepareHandsTheProcessorItsSpec") = []
{
    auto rig = Rig {};
    auto processor = FakeProcessor {};
    processor.layout = BusLayout::instrument(4);

    rig.engine.prepare(processor, sampleRate, blockSize);

    check(processor.specs.size() == 1);

    auto expected = ProcessSpec {sampleRate, blockSize, BusLayout::instrument(4)};

    check(processor.specs[0] == expected);
    check(rig.engine.getSpec() == expected);
    check(processor.resets == 0);
    check(processor.processed == 0);
};

auto tDirtySameShape = test("Engine/aDirtyBlockOfTheSameShapeOnlyResets") = []
{
    auto rig = Rig {};
    auto processor = FakeProcessor {};
    auto stream = FakeStream {2, 2};

    rig.engine.prepare(processor, sampleRate, blockSize);
    stream.run(rig.engine, blockSize, true);

    check(processor.specs.size() == 1);
    check(processor.resets == 1);
    check(processor.processed == 1);
};

auto tCleanBlock = test("Engine/aCleanBlockNeitherPreparesNorResets") = []
{
    auto rig = Rig {};
    auto processor = FakeProcessor {};
    auto stream = FakeStream {2, 2};

    rig.engine.prepare(processor, sampleRate, blockSize);
    stream.run(rig.engine);
    stream.run(rig.engine);

    check(processor.specs.size() == 1);
    check(processor.resets == 0);
    check(processor.processed == 2);
};

auto tDirtyNewRate = test("Engine/aDirtyBlockAtANewRateReprepares") = []
{
    auto rig = Rig {};
    auto processor = FakeProcessor {};
    auto stream = FakeStream {2, 2};

    rig.engine.prepare(processor, sampleRate, blockSize);

    auto block = stream.info(blockSize, true, 44100, blockSize);
    rig.engine.process(block);

    check(processor.specs.size() == 2);
    check(processor.specs[1].sampleRate == 44100);
    check(processor.specs[1].maxBlockSize == blockSize);
    check(processor.specs[1].layout == processor.layout);
    check(rig.engine.getSpec() == processor.specs[1]);
    check(processor.resets == 1);
    check(processor.processed == 1);
};

auto tDirtyNewBlock = test("Engine/aDirtyBlockWithALargerBlockSizeReprepares") = []
{
    auto rig = Rig {};
    auto processor = FakeProcessor {};
    processor.writePattern = true;
    auto stream = FakeStream {2, 2};

    rig.engine.prepare(processor, sampleRate, blockSize);

    auto block = stream.info(512, true, sampleRate, 512);
    rig.engine.process(block);

    check(processor.specs.size() == 2);
    check(processor.specs[1].sampleRate == sampleRate);
    check(processor.specs[1].maxBlockSize == 512);
    check(processor.resets == 1);
    check(stream.outputHoldsPattern(0, 0, 512));
    check(stream.outputHoldsPattern(1, 1, 512));
};

auto tDirtyOversizeBlock = test("Engine/aDirtyBlockLongerThanTheSpecReprepares") = []
{
    // The device still claims the old maximum, but the block itself overruns it.
    auto rig = Rig {};
    auto processor = FakeProcessor {};
    auto stream = FakeStream {2, 2};

    rig.engine.prepare(processor, sampleRate, blockSize);

    auto block = stream.info(512, true, sampleRate, blockSize);
    rig.engine.process(block);

    check(processor.specs.size() == 2);
    check(processor.specs[1].maxBlockSize >= 512);
    check(processor.resets == 1);
};

auto tStereoOnStereo = test("Engine/aStereoBusWritesAStereoDevice") = []
{
    auto rig = Rig {};
    auto processor = FakeProcessor {};
    processor.writePattern = true;
    auto stream = FakeStream {2, 2};

    rig.engine.prepare(processor, sampleRate, blockSize);
    stream.run(rig.engine, blockSize, true);

    check(stream.outputHoldsPattern(0, 0));
    check(stream.outputHoldsPattern(1, 1));
};

auto tStereoOnQuad = test("Engine/deviceChannelsPastTheBusAreCleared") = []
{
    auto rig = Rig {};
    auto processor = FakeProcessor {};
    processor.writePattern = true;
    auto stream = FakeStream {2, 4};

    rig.engine.prepare(processor, sampleRate, blockSize);
    stream.run(rig.engine, blockSize, true);

    check(stream.outputHoldsPattern(0, 0));
    check(stream.outputHoldsPattern(1, 1));
    check(stream.outputIs(2, 0.f));
    check(stream.outputIs(3, 0.f));
};

auto tStereoOnMono = test("Engine/aStereoBusOnAMonoDeviceKeepsItsFirstChannel") = []
{
    auto rig = Rig {};
    auto processor = FakeProcessor {};
    processor.writePattern = true;

    auto busChannels = 0;
    auto busSamples = 0;
    processor.onProcess = [&](ProcessContext& context)
    {
        busChannels = context.mainOutput().getNumChannels();
        busSamples = context.mainOutput().getNumSamples();
    };

    auto stream = FakeStream {1, 1};

    rig.engine.prepare(processor, sampleRate, blockSize);
    stream.run(rig.engine, blockSize, true);
    stream.run(rig.engine);

    check(busChannels == 2);
    check(busSamples == blockSize);
    check(stream.outputHoldsPattern(0, 0));
};

auto tOutputClearedFirst = test("Engine/theOutputBusIsClearedBeforeProcess") = []
{
    auto rig = Rig {};
    auto processor = FakeProcessor {};

    auto sawGarbage = false;
    processor.onProcess = [&](ProcessContext& context)
    {
        for (auto channel: context.mainOutput())
            for (auto sample: channel)
                sawGarbage = sawGarbage || sample != 0.f;
    };

    auto stream = FakeStream {2, 2};

    rig.engine.prepare(processor, sampleRate, blockSize);
    stream.run(rig.engine, blockSize, true);

    check(!sawGarbage);
    check(stream.outputIs(0, 0.f));
    check(stream.outputIs(1, 0.f));
};

auto tShortBlock = test("Engine/aShortBlockBindsOnlyItsSamples") = []
{
    auto rig = Rig {};
    auto processor = FakeProcessor {};
    processor.writePattern = true;
    auto stream = FakeStream {2, 2};

    rig.engine.prepare(processor, sampleRate, blockSize);
    stream.run(rig.engine, 100, true);

    check(stream.outputHoldsPattern(0, 0, 100));
    check(stream.outputHoldsPattern(1, 1, 100));
    check(stream.outputs[0][100] == garbage);
};

// Reads the first and last sample of every main-input channel the processor sees.
struct InputSpy
{
    void attach(FakeProcessor& processor)
    {
        processor.onProcess = [this](ProcessContext& context)
        {
            const auto& input = context.mainInput();
            channels = input.getNumChannels();
            samples = input.getNumSamples();
            first.clear();
            last.clear();

            for (auto ch = 0; ch < channels; ++ch)
            {
                first.push_back(input[ch][0]);
                last.push_back(input[ch][samples - 1]);
            }
        };
    }

    int channels = -1;
    int samples = -1;
    std::vector<float> first;
    std::vector<float> last;
};

auto tInputStereo = test("Engine/aStereoBusReadsAStereoDevice") = []
{
    auto rig = Rig {};
    auto processor = FakeProcessor {};
    auto spy = InputSpy {};
    spy.attach(processor);

    auto stream = FakeStream {2, 2};
    stream.fillInputs();

    rig.engine.prepare(processor, sampleRate, blockSize);
    stream.run(rig.engine, blockSize, true);

    check(spy.channels == 2);
    check(spy.samples == blockSize);
    check(spy.first[0] == patternAt(10, 0));
    check(spy.first[1] == patternAt(11, 0));
    check(spy.last[1] == patternAt(11, blockSize - 1));
};

auto tInputMono = test("Engine/aStereoBusOnAMonoMicHearsItOnBothSides") = []
{
    auto rig = Rig {};
    auto processor = FakeProcessor {};
    auto spy = InputSpy {};
    spy.attach(processor);

    auto stream = FakeStream {1, 2};
    stream.fillInputs();

    rig.engine.prepare(processor, sampleRate, blockSize);
    stream.run(rig.engine, blockSize, true);

    check(spy.channels == 2);
    check(spy.first[0] == patternAt(10, 0));
    check(spy.first[1] == patternAt(10, 0));
    check(spy.last[1] == patternAt(10, blockSize - 1));
};

auto tInputNone = test("Engine/anInputBusOnADeviceWithNoInputsReadsSilence") = []
{
    auto rig = Rig {};
    auto processor = FakeProcessor {};
    auto spy = InputSpy {};
    spy.attach(processor);

    auto stream = FakeStream {0, 2};

    rig.engine.prepare(processor, sampleRate, blockSize);
    stream.run(rig.engine, blockSize, true);

    check(spy.channels == 2);
    check(spy.samples == blockSize);
    check(spy.first[0] == 0.f);
    check(spy.first[1] == 0.f);
    check(spy.last[0] == 0.f);
    check(spy.last[1] == 0.f);
};

auto tNoOutputBus = test("Engine/aLayoutWithNoOutputClearsEveryDeviceChannel") = []
{
    auto rig = Rig {};
    auto processor = FakeProcessor {};
    processor.layout = layoutWith(2, 0);
    processor.writePattern = true;

    auto busChannels = -1;
    processor.onProcess = [&](ProcessContext& context)
    { busChannels = context.mainOutput().getNumChannels(); };

    auto stream = FakeStream {2, 2};

    rig.engine.prepare(processor, sampleRate, blockSize);
    stream.run(rig.engine, blockSize, true);

    check(processor.processed == 1);
    check(busChannels == 0);
    check(stream.outputIs(0, 0.f));
    check(stream.outputIs(1, 0.f));
};

auto tNoInputBus = test("Engine/aLayoutWithNoInputStillProcesses") = []
{
    auto rig = Rig {};
    auto processor = FakeProcessor {};
    processor.layout = BusLayout::stereoOut();
    processor.writePattern = true;

    auto inputEmpty = false;
    processor.onProcess = [&](ProcessContext& context)
    { inputEmpty = context.mainInput().isEmpty(); };

    auto stream = FakeStream {2, 2};
    stream.fillInputs();

    rig.engine.prepare(processor, sampleRate, blockSize);
    stream.run(rig.engine, blockSize, true);

    check(processor.processed == 1);
    check(inputEmpty);
    check(stream.outputHoldsPattern(0, 0));
    check(stream.outputHoldsPattern(1, 1));
};

auto tAuxBuses = test("Engine/auxBusesStayEmpty") = []
{
    auto rig = Rig {};
    auto processor = FakeProcessor {};
    processor.layout = BusLayout::stereoInOut();
    processor.layout.inputs.add({"Sidechain", 2, false});
    processor.layout.outputs.add({"Aux", 2, false});

    auto auxEmpty = false;
    processor.onProcess = [&](ProcessContext& context)
    {
        auxEmpty = context.inputs.size() == 2 && context.outputs.size() == 2
                   && context.inputs[1].isEmpty() && context.outputs[1].isEmpty();
    };

    auto stream = FakeStream {4, 4};

    rig.engine.prepare(processor, sampleRate, blockSize);
    stream.run(rig.engine, blockSize, true);

    check(auxEmpty);
};

auto tNoProcessor = test("Engine/withNoProcessorTheOutputsAreCleared") = []
{
    auto rig = Rig {};
    auto stream = FakeStream {2, 3};

    stream.run(rig.engine, blockSize, true);
    stream.run(rig.engine);

    check(stream.outputIs(0, 0.f));
    check(stream.outputIs(1, 0.f));
    check(stream.outputIs(2, 0.f));
};

auto tMidiOutCleared = test("Engine/midiOutIsEmptyAtTheStartOfEveryBlock") = []
{
    auto rig = Rig {};
    auto processor = FakeProcessor {};
    processor.layout = BusLayout::instrument();
    processor.layout.midiOutputs.add({"MIDI Out"});
    processor.writeMidiOut = true;

    auto stream = FakeStream {0, 2};

    rig.engine.prepare(processor, sampleRate, blockSize);
    stream.run(rig.engine, blockSize, true);
    check(processor.midiOutWasEmpty);

    stream.run(rig.engine);
    check(processor.midiOutWasEmpty);

    stream.run(rig.engine);
    check(processor.midiOutWasEmpty);
    check(processor.processed == 3);
};

const std::string loopbackName = "MakeASound Engine Test";

// Queue mode, so MidiBlockSync can drain it; false where there are no virtual
// ports, as in MidiManagerTests.
bool openLoopback(MidiManager& midi)
{
    if (!midi.isAvailable())
        return false;

    if (!midi.openVirtualInput(loopbackName).has_value())
        return false;

    for (const auto& port: midi.getOutputPorts())
        if (port.name.find(loopbackName) != std::string::npos)
            return midi.openOutput(port.id) == Error::NoError;

    midi.closeAllInputs();
    return false;
}

auto tMidiAlignment = test("Engine/loopbackMidiLandsInsideTheBlock") = []
{
    auto rig = Rig {};

    if (!openLoopback(rig.midi))
        return;

    auto processor = FakeProcessor {};
    processor.layout = BusLayout::instrument();

    auto noteOns = 0;
    auto others = 0;
    auto inRange = true;

    processor.onProcess = [&](ProcessContext& context)
    {
        for (const auto& event: context.mainMidiIn())
        {
            if (event.isNoteOn())
                ++noteOns;
            else
                ++others;

            inRange =
                inRange && event.sampleOffset >= 0 && event.sampleOffset < blockSize;
        }
    };

    auto stream = FakeStream {0, 2};

    rig.engine.prepare(processor, sampleRate, blockSize);
    stream.run(rig.engine, blockSize, true);

    auto sent = rig.midi.sendMessage(Event::noteOn(0, 60, 1.f));

    std::this_thread::sleep_for(10ms);
    stream.run(rig.engine);

    for (auto block = 0; block < 200 && noteOns == 0; ++block)
    {
        std::this_thread::sleep_for(5ms);
        stream.run(rig.engine);
    }

    rig.midi.closeOutput();
    rig.midi.closeAllInputs();

    if (sent != Error::NoError || noteOns == 0)
        return;

    check(noteOns == 1);
    check(others == 0);
    check(inRange);
};
} // namespace
