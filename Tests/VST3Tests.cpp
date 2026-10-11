// The VST3 adapter hosted in-process: the factory, buses and arrangements, a
// whole block, parameter info and values, the automation queue, the edit gate,
// state through the component, MIDI in and out, CC through IMidiMapping, latency
// reporting and the editor view.

#include "VST3TestHost.h"

#include <MakeASound/Plugin/VST3/Factory.h>
#include <MakeASound/Plugin/VST3/Text.h>

#include <pluginterfaces/gui/iplugview.h>
#include <pluginterfaces/vst/ivstmidicontrollers.h>

#if MAKEASOUND_VST3_HAS_VIEW
#include <MakeASound/Plugin/UI/GenericEditor.h>
#include <MakeASound/Plugin/VST3/PlugView.h>
#endif

#include <NanoTest/NanoTest.h>

#include <eacp/Core/Threads/EventLoop.h>

#include <climits>
#include <cmath>
#include <set>
#include <string>
#include <thread>

using namespace nano;
using namespace TestPlugins;
using namespace VST3Host;
using MakeASound::VST3::Adapter;

namespace
{
struct LatencyParams : ParameterGroup
{
    LatencyParams() { add(latency, hidden); }

    FloatParam latency {"Latency", 0.f, 1000.f, 0.f};
    FloatParam hidden {"Hidden", 0.f, 1.f, 0.f, {.automatable = false}};
};

// latencySamples() is a parameter's value.
struct LatencyPlugin : StatePlugin<State<LatencyParams>>
{
    std::string_view name() const override { return "Latency"; }

    void prepare(const ProcessSpec&) override {}
    void process(ProcessContext&) noexcept override {}

    int latencySamples() const noexcept override
    {
        return static_cast<int>(std::lround(params.latency.getValue()));
    }

    int tailSamples() const noexcept override { return tail; }

    void announceLatency() { notifyHostLatencyChanged(); }

    int tail = 0;
};

bool near(double a, double b, double tolerance = 1e-4)
{
    return std::abs(a - b) < tolerance;
}

template <typename Predicate>
bool pumpUntil(Predicate ready)
{
    return eacp::Threads::runEventLoopUntil(ready, eacp::Time::MS {5000});
}

std::string streamText(Steinberg::MemoryStream& stream)
{
    return {stream.getData(), static_cast<size_t>(stream.getSize())};
}

void rewind(Steinberg::IBStream& stream)
{
    stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
}

Vst::Event noteEvent(bool on, int offset, int bus = 0)
{
    auto event = Vst::Event {};
    event.busIndex = bus;
    event.sampleOffset = offset;

    if (on)
    {
        event.type = Vst::Event::kNoteOnEvent;
        event.noteOn.channel = 2;
        event.noteOn.pitch = 64;
        event.noteOn.velocity = 0.75f;
        event.noteOn.noteId = -1;
    }
    else
    {
        event.type = Vst::Event::kNoteOffEvent;
        event.noteOff.channel = 2;
        event.noteOff.pitch = 64;
        event.noteOff.velocity = 0.5f;
        event.noteOff.noteId = -1;
    }

    return event;
}

Vst::Event outputEvent(AdapterHost<SynthPlugin>& host, int index)
{
    auto event = Vst::Event {};
    host.outputEvents.getEvent(index, event);
    return event;
}

const ModuleDescription& testModule()
{
    static const auto module = []
    {
        auto description = ModuleDescription {};
        description.vendor = "MakeASound Tests";
        description.url = "https://example.com";
        description.manufacturerCode = "MaSo";
        description.plugins.add(
            {.name = "Gain",
             .version = "1.2.3",
             .pluginCode = "Gain",
             .create = [] { return EA::makeOwned<GainPlugin>(); }});
        description.plugins.add(
            {.name = "Synth",
             .category = Category::Instrument,
             .pluginCode = "Synt",
             .create = [] { return EA::makeOwned<SynthPlugin>(); }});
        return description;
    }();

    return module;
}

auto tFactoryClasses = test("VST3/theFactoryListsEveryPlugin") = []
{
    const auto& module = testModule();
    auto factory = Steinberg::owned(MakeASound::VST3::makeFactory(module));

    check(factory->countClasses() == 2);

    auto factoryInfo = Steinberg::PFactoryInfo {};
    check(factory->getFactoryInfo(&factoryInfo) == Steinberg::kResultOk);
    check(std::string(factoryInfo.vendor) == "MakeASound Tests");

    auto subcategories = std::array<std::string, 2> {"Fx", "Instrument|Synth"};

    for (auto i = 0; i < 2; ++i)
    {
        auto info = Steinberg::PClassInfo2 {};
        check(factory->getClassInfo2(i, &info) == Steinberg::kResultOk);
        check(std::string(info.category) == kVstAudioEffectClass);
        check(std::string(info.subCategories) == subcategories[i]);
        check(info.classFlags == 0);
        check(Steinberg::FUID::fromTUID(info.cid)
              == MakeASound::VST3::classIdFor(module, module.plugins[i]));
        check(std::string(info.version) == module.plugins[i].version);
        check(std::string(info.vendor) == "MakeASound Tests");
    }
};

auto tFactoryCreates = test("VST3/createInstanceAnswersEveryInterface") = []
{
    const auto& module = testModule();
    auto factory = Steinberg::owned(MakeASound::VST3::makeFactory(module));
    auto cid = MakeASound::VST3::classIdFor(module, module.plugins[1]);

    auto* object = static_cast<void*>(nullptr);
    check(
        factory->createInstance(reinterpret_cast<Steinberg::FIDString>(cid.toTUID()),
                                Vst::IComponent::iid,
                                &object)
        == Steinberg::kResultOk);
    check(object != nullptr);

    auto component = Steinberg::owned(static_cast<Vst::IComponent*>(object));

    auto answers = [&](const Steinberg::TUID iid)
    {
        auto* other = static_cast<void*>(nullptr);

        if (component->queryInterface(iid, &other) != Steinberg::kResultOk)
            return false;

        static_cast<Steinberg::FUnknown*>(other)->release();
        return true;
    };

    check(answers(Vst::IAudioProcessor::iid));
    check(answers(Vst::IEditController::iid));
    check(answers(Vst::IMidiMapping::iid));

    auto unknown = Steinberg::FUID(1, 2, 3, 4);
    auto* none = static_cast<void*>(nullptr);
    check(factory->createInstance(
              reinterpret_cast<Steinberg::FIDString>(unknown.toTUID()),
              Vst::IComponent::iid,
              &none)
          == Steinberg::kNoInterface);
    check(none == nullptr);
};

auto tBuses = test("VST3/busesFollowTheLayout") = []
{
    auto effect = AdapterHost<GainPlugin> {};
    auto& gain = *effect.adapter;

    check(gain.getBusCount(Vst::kAudio, Vst::kInput) == 1);
    check(gain.getBusCount(Vst::kAudio, Vst::kOutput) == 1);
    check(gain.getBusCount(Vst::kEvent, Vst::kInput) == 0);

    for (auto direction: {Vst::kInput, Vst::kOutput})
    {
        auto info = Vst::BusInfo {};
        check(gain.getBusInfo(Vst::kAudio, direction, 0, info)
              == Steinberg::kResultOk);
        check(info.channelCount == 2);
        check(info.busType == Vst::kMain);
        check(info.flags == Vst::BusInfo::kDefaultActive);
    }

    auto instrument = AdapterHost<SynthPlugin> {};
    auto& synth = *instrument.adapter;

    check(synth.getBusCount(Vst::kAudio, Vst::kInput) == 0);
    check(synth.getBusCount(Vst::kAudio, Vst::kOutput) == 1);
    check(synth.getBusCount(Vst::kEvent, Vst::kInput) == 1);
    check(synth.getBusCount(Vst::kEvent, Vst::kOutput) == 1);

    auto info = Vst::BusInfo {};
    check(synth.getBusInfo(Vst::kAudio, Vst::kOutput, 0, info)
          == Steinberg::kResultOk);
    check(info.channelCount == 2 && info.flags == Vst::BusInfo::kDefaultActive);

    for (auto direction: {Vst::kInput, Vst::kOutput})
    {
        check(synth.getBusInfo(Vst::kEvent, direction, 0, info)
              == Steinberg::kResultOk);
        check(info.channelCount == 16);
        check(info.flags == Vst::BusInfo::kDefaultActive);
    }
};

auto tArrangements = test("VST3/busArrangementsNegotiateThroughTheWrapper") = []
{
    auto host = AdapterHost<GainPlugin> {};
    auto& adapter = *host.adapter;
    auto mono = Vst::SpeakerArrangement {Vst::SpeakerArr::kMono};
    auto stereo = Vst::SpeakerArrangement {Vst::SpeakerArr::kStereo};

    adapter.setProcessing(false);
    adapter.setActive(false);

    check(adapter.setBusArrangements(&mono, 1, &mono, 1) == Steinberg::kResultTrue);
    check(adapter.wrapper().busLayout().inputs[0].numChannels == 1);
    check(adapter.wrapper().busLayout().outputs[0].numChannels == 1);

    check(adapter.setBusArrangements(&mono, 1, &stereo, 1)
          == Steinberg::kResultFalse);

    auto arrangement = Vst::SpeakerArrangement {};
    check(adapter.getBusArrangement(Vst::kOutput, 0, arrangement)
          == Steinberg::kResultOk);
    check(arrangement == Vst::SpeakerArr::kMono);

    adapter.setActive(true);
    check(adapter.setBusArrangements(&mono, 1, &mono, 1) == Steinberg::kResultFalse);

    check(adapter.canProcessSampleSize(Vst::kSample32) == Steinberg::kResultTrue);
    check(adapter.canProcessSampleSize(Vst::kSample64) == Steinberg::kResultFalse);
};

auto tActivation = test("VST3/activationPreparesThePlugin") = []
{
    auto host = AdapterHost<GainPlugin> {};
    auto& plugin = host.plugin();

    check(plugin.spec.sampleRate == 48000);
    check(plugin.spec.maxBlockSize == blockSize);
    check(plugin.format() == PluginFormat::VST3);
    check(plugin.formatAtPrepare == PluginFormat::VST3);
};

auto tUnity = test("VST3/aBlockAtUnityPassesTheInput") = []
{
    for (auto inPlace: {false, true})
    {
        auto host = AdapterHost<GainPlugin> {};

        for (auto ch = 0; ch < 2; ++ch)
            for (auto s = 0; s < blockSize; ++s)
                host.inputs[0].storage[ch][s] = 0.01f * static_cast<float>(s + ch);

        auto expected = host.inputs[0].storage;

        host.fillBlock(blockSize, inPlace);
        check(host.run() == Steinberg::kResultOk);
        check(host.plugin().processed == 1);

        auto& output = inPlace ? host.inputs[0] : host.outputs[0];
        check(output.storage == expected);
        check(host.outputBuses[0].silenceFlags == 0);
    }
};

auto tPlayhead = test("VST3/thePlayheadArrivesConverted") = []
{
    auto host = AdapterHost<GainPlugin> {};

    host.context.state = Vst::ProcessContext::kPlaying
                         | Vst::ProcessContext::kTempoValid
                         | Vst::ProcessContext::kTimeSigValid;
    host.context.tempo = 140.0;
    host.context.timeSigNumerator = 7;
    host.context.timeSigDenominator = 8;
    host.context.projectTimeSamples = 4800;
    host.context.projectTimeMusic = 12.0;

    host.fillBlock(blockSize);
    host.run();

    const auto& playhead = host.plugin().playhead;
    check(playhead.isValid);
    check(playhead.isPlaying);
    check(!playhead.isLooping);
    check(playhead.bpm == 140.0);
    check(playhead.timeSignature.numerator == 7);
    check(playhead.timeSignature.denominator == 8);
    check(playhead.sampleTime == 4800);
    check(playhead.ppqPosition == 0.0);
};

auto tBeforeActivation = test("VST3/aBlockBeforeActivationIsSilent") = []
{
    auto adapter = Steinberg::owned(new Adapter(EA::makeOwned<GainPlugin>()));
    adapter->initialize(nullptr);

    auto input = HostBus(2);
    auto output = HostBus(2);

    for (auto* bus: {&input, &output})
        for (auto& channel: bus->storage)
            std::fill(channel.begin(), channel.end(), 1.f);

    auto buses = std::array<Vst::AudioBusBuffers, 2> {};
    buses[0].numChannels = 2;
    buses[0].channelBuffers32 = input.pointers.data();
    buses[1].numChannels = 2;
    buses[1].channelBuffers32 = output.pointers.data();

    auto data = Vst::ProcessData {};
    data.symbolicSampleSize = Vst::kSample32;
    data.numSamples = blockSize;
    data.numInputs = 1;
    data.numOutputs = 1;
    data.inputs = &buses[0];
    data.outputs = &buses[1];

    check(adapter->process(data) == Steinberg::kResultOk);

    for (const auto& channel: output.storage)
        for (auto sample: channel)
            check(sample == 0.f);

    check(static_cast<GainPlugin&>(adapter->plugin()).processed == 0);

    data.symbolicSampleSize = Vst::kSample64;
    check(adapter->process(data) == Steinberg::kResultFalse);

    adapter->terminate();
};

auto tParameterInfo = test("VST3/parameterInfoMirrorsTheList") = []
{
    auto host = AdapterHost<GainPlugin> {};
    auto& adapter = *host.adapter;
    const auto& list = adapter.plugin().parameters();

    check(adapter.getParameterCount() == list.size());

    for (auto i = 0; i < list.size(); ++i)
    {
        const auto& entry = list.entry(i);
        const auto& param = list[i];
        auto info = Vst::ParameterInfo {};

        check(adapter.getParameterInfo(i, info) == Steinberg::kResultOk);
        check(info.id == entry.hostId);
        check(MakeASound::VST3::toUtf8(info.title) == entry.displayName);
        check(MakeASound::VST3::toUtf8(info.units) == param.label());
        check(info.stepCount == param.numSteps());
        check(near(info.defaultNormalizedValue,
                   param.toNormalized(param.defaultValue())));
        check((info.flags & Vst::ParameterInfo::kCanAutomate) != 0);

        auto isList = (info.flags & Vst::ParameterInfo::kIsList) != 0;
        auto isBypass = (info.flags & Vst::ParameterInfo::kIsBypass) != 0;
        check(isList == (&param == &host.plugin().params.mode));
        check(isBypass == (&param == &host.plugin().params.bypass));
    }

    auto instrument = AdapterHost<SynthPlugin> {};
    auto& synth = *instrument.adapter;
    auto exposed = synth.plugin().parameters().size();

    check(synth.getParameterCount() == exposed + 2096);

    for (auto i = exposed; i < synth.getParameterCount(); ++i)
    {
        auto info = Vst::ParameterInfo {};
        synth.getParameterInfo(i, info);
        check((info.flags & Vst::ParameterInfo::kIsHidden) != 0);
        check(info.id >= 0x7fff0000u);
    }
};

auto tNormalized = test("VST3/normalizedValuesAreReadLive") = []
{
    auto host = AdapterHost<GainPlugin> {};
    auto& adapter = *host.adapter;
    auto& params = host.plugin().params;
    auto gainId = host.hostIdOf(params.gain);

    check(adapter.setParamNormalized(gainId, 0.25) == Steinberg::kResultTrue);
    check(near(params.gain.getNormalized(), 0.25));
    check(near(adapter.getParamNormalized(gainId), 0.25));

    params.gain.setNormalized(0.8f);
    check(near(adapter.getParamNormalized(gainId), 0.8));
};

auto tText = test("VST3/textAndPlainGoThroughTheParameter") = []
{
    auto host = AdapterHost<GainPlugin> {};
    auto& adapter = *host.adapter;
    auto& params = host.plugin().params;

    for (auto* param: std::initializer_list<MakeASound::Parameter*> {
             &params.gain, &params.filter.cutoff, &params.mode})
    {
        auto id = host.hostIdOf(*param);
        auto value = 0.4;

        Vst::String128 text {};
        check(adapter.getParamStringByValue(id, value, text)
              == Steinberg::kResultOk);

        auto expected =
            param->valueToText(param->toPlain(static_cast<float>(value)));
        check(MakeASound::VST3::toUtf8(text) == expected);

        auto parsed = Vst::ParamValue {-1.0};
        check(adapter.getParamValueByString(id, text, parsed)
              == Steinberg::kResultOk);
        check(near(parsed, param->toNormalized(param->textToValue(expected))));
    }

    auto& cutoff = params.filter.cutoff;
    auto cutoffId = host.hostIdOf(cutoff);
    check(near(
        adapter.normalizedParamToPlain(cutoffId, 0.5), cutoff.toPlain(0.5f), 1e-2));
    check(near(adapter.plainParamToNormalized(cutoffId, 1000.0),
               cutoff.toNormalized(1000.f)));
};

auto tQueue = test("VST3/theQueueLeavesTheLastPoint") = []
{
    auto host = AdapterHost<GainPlugin> {};
    auto& params = host.plugin().params;
    auto gainId = host.hostIdOf(params.gain);

    host.addPoint(gainId, 0, 0.2);
    host.addPoint(gainId, 32, 0.6);
    host.fillBlock(blockSize);
    host.run();

    check(near(params.gain.getNormalized(), 0.6));
    check(host.plugin().processed == 1);

    host.clearQueues();
    host.addPoint(gainId, 0, 0.3);
    host.fillBlock(0);
    host.run();

    check(near(params.gain.getNormalized(), 0.3));
    check(host.plugin().processed == 1);
};

// The host automates the gain with a zero-sample flush.
void hostAutomates(AdapterHost<GainPlugin>& host, double value)
{
    host.clearQueues();
    host.addPoint(host.hostIdOf(host.plugin().params.gain), 0, value);
    host.fillBlock(0);
    host.run();
}

auto tGateHolds = test("VST3/aHoldDropsTheQueueAndTheController") = []
{
    auto host = AdapterHost<GainPlugin> {};
    auto& gain = host.plugin().params.gain;
    auto gainId = host.hostIdOf(gain);
    auto& listener = *host.plugin().hostEditListener();

    listener.beginParameterEdit(0);
    gain.setNormalized(0.2f);
    listener.performParameterEdit(0, 0.2f);

    hostAutomates(host, 0.7);
    host.adapter->setParamNormalized(gainId, 0.7);
    check(near(gain.getNormalized(), 0.2));

    listener.endParameterEdit(0);

    hostAutomates(host, 0.4);
    check(near(gain.getNormalized(), 0.4));

    host.adapter->setParamNormalized(gainId, 0.6);
    check(near(gain.getNormalized(), 0.6));
};

auto tGateNests = test("VST3/nestedHoldsReleaseAtTheOutermostEnd") = []
{
    auto host = AdapterHost<GainPlugin> {};
    auto& gain = host.plugin().params.gain;
    auto& listener = *host.plugin().hostEditListener();

    listener.beginParameterEdit(0);
    listener.beginParameterEdit(0);
    gain.setNormalized(0.1f);
    listener.endParameterEdit(0);

    hostAutomates(host, 0.7);
    check(near(gain.getNormalized(), 0.1));

    listener.endParameterEdit(0);

    hostAutomates(host, 0.8);
    check(near(gain.getNormalized(), 0.8));
};

auto tGateUnderflow = test("VST3/anUnmatchedEndCannotUnderflow") = []
{
    auto host = AdapterHost<GainPlugin> {};
    auto& gain = host.plugin().params.gain;
    auto& listener = *host.plugin().hostEditListener();

    listener.endParameterEdit(0);
    listener.beginParameterEdit(0);
    gain.setNormalized(0.1f);

    hostAutomates(host, 0.9);
    check(near(gain.getNormalized(), 0.1));

    listener.endParameterEdit(0);
};

auto tEditsReachHandler = test("VST3/editsReachTheHandlerByHostId") = []
{
    using Kind = TestComponentHandler::EditKind;

    auto host = AdapterHost<LatencyPlugin> {};
    auto& listener = *host.plugin().hostEditListener();
    auto& edits = host.handler.edits;
    auto latencyId = host.hostIdOf(host.plugin().params.latency);

    listener.beginParameterEdit(0);
    listener.performParameterEdit(0, 0.5f);
    listener.endParameterEdit(0);

    check(edits.size() == 3);
    check(edits[0].kind == Kind::Begin && edits[0].id == latencyId);
    check(edits[1].kind == Kind::Perform && edits[1].id == latencyId);
    check(near(edits[1].value, 0.5));
    check(edits[2].kind == Kind::End && edits[2].id == latencyId);

    listener.beginParameterEdit(1);
    check(host.adapter->wrapper().isParameterHeld(1));
    listener.performParameterEdit(1, 0.5f);
    listener.endParameterEdit(1);
    check(!host.adapter->wrapper().isParameterHeld(1));
    check(edits.size() == 3);

    listener.beginParameterEditGroup();
    listener.endParameterEditGroup();
    check(host.handler.groupStarts == 1);
    check(host.handler.groupEnds == 1);
};

void moveEveryParameter(GainParams& params)
{
    params.gain.setValue(-12.f);
    params.filter.cutoff.setValue(440.f);
    params.mode.setValue(2.f);
    params.bypass.setValue(1.f);
    params.width.setValue(0.9f);
}

auto tStateRoundTrip = test("VST3/stateRoundTripsThroughTheComponent") = []
{
    auto source = AdapterHost<GainPlugin> {};
    moveEveryParameter(source.plugin().params);

    auto stream = Steinberg::MemoryStream {};
    check(source.adapter->getState(&stream) == Steinberg::kResultOk);
    rewind(stream);

    auto target = AdapterHost<GainPlugin> {};
    check(target.adapter->setState(&stream) == Steinberg::kResultOk);

    const auto& params = target.plugin().params;
    check(near(params.gain.getValue(), -12.f));
    check(near(params.filter.cutoff.getValue(), 440.f, 1e-2));
    check(params.mode.getIndex() == 2);
    check(params.bypass.isOn());
    check(near(params.width.getValue(), 0.9f));

    check(target.adapter->setState(nullptr) == Steinberg::kInvalidArgument);
};

auto tPresetStream = test("VST3/aPresetStreamLeavesSessionOnlyParametersOut") = []
{
    auto source = AdapterHost<GainPlugin> {};
    moveEveryParameter(source.plugin().params);

    auto preset = TestAttributeStream(Vst::StateType::kDefault);
    check(source.adapter->getState(&preset) == Steinberg::kResultOk);
    check(streamText(preset).find("Width") == std::string::npos);
    check(streamText(preset).find("Gain") != std::string::npos);

    auto target = AdapterHost<GainPlugin> {};
    target.plugin().params.width.setValue(0.1f);
    rewind(preset);
    target.adapter->setState(&preset);

    check(near(target.plugin().params.gain.getValue(), -12.f));
    check(near(target.plugin().params.width.getValue(), 0.1f));

    auto project = TestAttributeStream(Vst::StateType::kProject);
    source.adapter->getState(&project);
    check(streamText(project).find("Width") != std::string::npos);
};

auto tSaveOffThread = test("VST3/getStateOffTheMessageThreadReadsTheSnapshot") = []
{
    auto host = AdapterHost<GainPlugin> {};
    host.plugin().params.gain.setValue(-6.f);

    auto stream = Steinberg::MemoryStream {};
    auto result = tresult {Steinberg::kResultFalse};
    auto saver = std::thread([&] { result = host.adapter->getState(&stream); });
    saver.join();

    check(result == Steinberg::kResultOk);
    check(stream.getSize() > 0);

    rewind(stream);
    auto target = AdapterHost<GainPlugin> {};
    target.adapter->setState(&stream);
    check(near(target.plugin().params.gain.getValue(), -6.f));
};

auto tLoadOffThread =
    test("VST3/setStateOffTheMessageThreadLandsParametersAtOnce") = []
{
    auto source = AdapterHost<GainPlugin> {};
    source.plugin().params.gain.setValue(-3.f);
    source.plugin().state.preset = "Later";
    source.plugin().state.markChanged();

    auto stream = Steinberg::MemoryStream {};
    source.adapter->getState(&stream);
    rewind(stream);

    auto target = AdapterHost<GainPlugin> {};
    auto loader = std::thread([&] { target.adapter->setState(&stream); });
    loader.join();

    check(near(target.plugin().params.gain.getValue(), -3.f));
    check(pumpUntil([&] { return target.plugin().state.preset == "Later"; }));
};

auto tControllerState = test("VST3/theControllerHalfOfStateIsNotImplemented") = []
{
    auto host = AdapterHost<GainPlugin> {};
    host.plugin().params.gain.setValue(-9.f);

    auto stream = Steinberg::MemoryStream {};
    auto* controller = static_cast<Vst::IEditController*>(host.adapter.get());

    // IEditController's setState/getState, under the names the SDK's rename
    // gives them once vstsinglecomponenteffect.h is included first.
    check(controller->setEditorState(&stream) == Steinberg::kNotImplemented);
    check(controller->getEditorState(&stream) == Steinberg::kNotImplemented);

    auto other = AdapterHost<GainPlugin> {};
    other.adapter->getState(&stream);
    rewind(stream);

    check(controller->setComponentState(&stream) == Steinberg::kResultOk);
    check(near(host.plugin().params.gain.getValue(), -9.f));
};

auto tMidiInOut = test("VST3/notesReachThePluginInOrderAndEchoBack") = []
{
    auto host = AdapterHost<SynthPlugin> {};

    host.addEvent(noteEvent(false, 10, INT_MIN));
    host.addEvent(noteEvent(true, 5));
    host.fillBlock(blockSize);
    host.run();

    const auto& plugin = host.plugin();
    check(plugin.numEvents == 2);
    check(plugin.events[0].isNoteOn() && plugin.events[0].sampleOffset == 5);
    check(plugin.events[0].channel == 2);
    check(plugin.events[0].asNoteOn()->pitch == 64);
    check(near(plugin.events[0].asNoteOn()->velocity, 0.75));
    check(plugin.events[1].isNoteOff() && plugin.events[1].sampleOffset == 10);

    check(host.outputEvents.getEventCount() == 2);

    auto on = outputEvent(host, 0);
    check(on.type == Vst::Event::kNoteOnEvent && on.sampleOffset == 5);
    check(on.noteOn.pitch == 64 && on.noteOn.channel == 2 && on.noteOn.noteId == -1);

    auto off = outputEvent(host, 1);
    check(off.type == Vst::Event::kNoteOffEvent && off.sampleOffset == 10);
};

auto tSysEx = test("VST3/aSysExRoundTrips") = []
{
    auto host = AdapterHost<SynthPlugin> {};
    auto bytes =
        std::array<Steinberg::uint8, 6> {0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7};

    auto event = Vst::Event {};
    event.type = Vst::Event::kDataEvent;
    event.sampleOffset = 3;
    event.data.type = Vst::DataEvent::kMidiSysEx;
    event.data.size = static_cast<Steinberg::uint32>(bytes.size());
    event.data.bytes = bytes.data();

    host.addEvent(event);
    host.fillBlock(blockSize);
    host.run();

    check(host.plugin().numEvents == 1);
    const auto* sysEx = host.plugin().events[0].asSysEx();
    check(sysEx != nullptr && sysEx->size == 6);
    check(std::equal(bytes.begin(), bytes.end(), sysEx->data.begin()));

    auto out = outputEvent(host, 0);
    check(out.type == Vst::Event::kDataEvent && out.sampleOffset == 3);
    check(out.data.type == Vst::DataEvent::kMidiSysEx && out.data.size == 6);
    check(std::equal(bytes.begin(), bytes.end(), out.data.bytes));
};

auto tCcMapping = test("VST3/ccArrivesThroughTheMapping") = []
{
    auto host = AdapterHost<SynthPlugin> {};
    auto id = Vst::ParamID {};

    check(host.adapter->getMidiControllerAssignment(0, 3, 74, id)
          == Steinberg::kResultTrue);
    check(id == MakeASound::VST3::shadowIdFor(0, 3, 74));

    host.addPoint(id, 0, 0.25);
    host.addPoint(id, 32, 0.75);
    host.fillBlock(blockSize);
    host.run();

    const auto& plugin = host.plugin();
    check(plugin.numEvents == 2);

    for (auto i = 0; i < 2; ++i)
    {
        const auto* cc = plugin.events[i].asControlChange();
        check(cc != nullptr && cc->controller == 74);
        check(plugin.events[i].channel == 3);
        check(plugin.events[i].sampleOffset == i * 32);
        check(near(cc->value, i == 0 ? 0.25 : 0.75));

        auto out = outputEvent(host, i);
        check(out.type == Vst::Event::kLegacyMIDICCOutEvent);
        check(out.midiCCOut.controlNumber == 74 && out.midiCCOut.channel == 3);
        check(out.midiCCOut.value == std::lround((i == 0 ? 0.25 : 0.75) * 127));
    }
};

auto tBendAndPressure = test("VST3/pitchBendAndAftertouchThroughTheMapping") = []
{
    auto host = AdapterHost<SynthPlugin> {};
    auto bend = Vst::ParamID {};
    auto pressure = Vst::ParamID {};

    host.adapter->getMidiControllerAssignment(0, 0, Vst::kPitchBend, bend);
    host.adapter->getMidiControllerAssignment(0, 0, Vst::kAfterTouch, pressure);

    host.addPoint(bend, 0, 0.75);
    host.addPoint(pressure, 1, 0.5);
    host.fillBlock(blockSize);
    host.run();

    const auto& plugin = host.plugin();
    check(plugin.numEvents == 2);
    check(plugin.events[0].isPitchBend());
    check(near(plugin.events[0].asPitchBend()->value, 0.5));
    check(plugin.events[1].isChannelAftertouch());
    check(near(plugin.events[1].asChannelAftertouch()->pressure, 0.5));

    auto out = outputEvent(host, 0);
    auto raw = (out.midiCCOut.value & 0x7f) | ((out.midiCCOut.value2 & 0x7f) << 7);
    check(out.type == Vst::Event::kLegacyMIDICCOutEvent);
    check(out.midiCCOut.controlNumber == Vst::kPitchBend);
    check(raw == 8192 + 4096);

    auto after = outputEvent(host, 1);
    check(after.midiCCOut.controlNumber == Vst::kAfterTouch);
    check(after.midiCCOut.value == 64);
};

auto tProgramChange = test("VST3/programChangeThroughTheMapping") = []
{
    auto host = AdapterHost<SynthPlugin> {};
    auto id = Vst::ParamID {};

    check(
        host.adapter->getMidiControllerAssignment(0, 5, Vst::kCtrlProgramChange, id)
        == Steinberg::kResultTrue);

    host.addPoint(id, 0, 10.0 / 127.0);
    host.fillBlock(blockSize);
    host.run();

    const auto* program = host.plugin().events[0].asProgramChange();
    check(program != nullptr && program->program == 10);
    check(host.plugin().events[0].channel == 5);

    auto out = outputEvent(host, 0);
    check(out.type == Vst::Event::kLegacyMIDICCOutEvent);
    check(out.midiCCOut.controlNumber == Vst::kCtrlProgramChange);
    check(out.midiCCOut.value == 10 && out.midiCCOut.channel == 5);
};

auto tMappingLimits = test("VST3/theMappingRefusesWhatItCannotCarry") = []
{
    using MakeASound::VST3::decodeShadowId;
    using MakeASound::VST3::shadowIdFor;

    auto host = AdapterHost<SynthPlugin> {};
    auto& adapter = *host.adapter;
    auto id = Vst::ParamID {};

    check(adapter.getMidiControllerAssignment(1, 0, 1, id)
          == Steinberg::kResultFalse);
    check(adapter.getMidiControllerAssignment(0, 16, 1, id)
          == Steinberg::kResultFalse);
    check(adapter.getMidiControllerAssignment(0, -1, 1, id)
          == Steinberg::kResultFalse);
    check(adapter.getMidiControllerAssignment(0, 0, 131, id)
          == Steinberg::kResultFalse);

    auto effect = AdapterHost<GainPlugin> {};
    check(effect.adapter->getMidiControllerAssignment(0, 0, 1, id)
          == Steinberg::kResultFalse);

    auto ids = std::set<Vst::ParamID> {};

    for (auto channel = 0; channel < 16; ++channel)
    {
        for (auto controller = 0; controller < 131; ++controller)
        {
            auto shadow = shadowIdFor(0, channel, controller);
            ids.insert(shadow);
            check(adapter.plugin().parameters().indexOfHostId(shadow) < 0);

            auto decoded = decodeShadowId(shadow, 1);
            check(decoded.has_value());
            check(decoded->bus == 0 && decoded->channel == channel
                  && decoded->controller == controller);
        }
    }

    check(ids.size() == 2096);
    check(!decodeShadowId(shadowIdFor(1, 0, 0), 1));
    check(!decodeShadowId(shadowIdFor(0, 0, 131), 1));
    check(!decodeShadowId(42, 1));
};

auto tLatency = test("VST3/latencyRestartsOnceTheHostHasABaseline") = []
{
    auto host = AdapterHost<LatencyPlugin> {};
    auto& adapter = *host.adapter;
    auto latencyId = host.hostIdOf(host.plugin().params.latency);
    auto latencyRestarts = [&]
    { return host.handler.restartsWith(Vst::kLatencyChanged); };

    adapter.setParamNormalized(latencyId, 0.5);
    check(latencyRestarts() == 0);

    check(adapter.getLatencySamples() == 500);

    adapter.setParamNormalized(latencyId, 0.25);
    check(latencyRestarts() == 1);

    adapter.setParamNormalized(latencyId, 0.25);
    check(latencyRestarts() == 1);

    host.plugin().params.latency.setValue(100.f);
    host.plugin().announceLatency();
    check(latencyRestarts() == 2);

    auto other = AdapterHost<LatencyPlugin> {};
    other.plugin().params.latency.setValue(700.f);

    auto stream = Steinberg::MemoryStream {};
    other.adapter->getState(&stream);
    rewind(stream);

    adapter.setState(&stream);
    check(latencyRestarts() == 3);
    check(adapter.getLatencySamples() == 700);

    check(adapter.getTailSamples() == Vst::kNoTail);
    host.plugin().tail = 256;
    check(adapter.getTailSamples() == 256);
};

auto tInfoChanged = test("VST3/parameterInfoChangedRestartsTitles") = []
{
    auto host = AdapterHost<LatencyPlugin> {};

    host.plugin().notifyHostParameterInfoChanged();
    check(host.handler.restartsWith(Vst::kParamTitlesChanged) == 1);
};

#if MAKEASOUND_VST3_HAS_VIEW
auto tView = test("VST3/theEditorViewIsTheGenericEditor") = []
{
    auto host = AdapterHost<GainPlugin> {};
    auto view = Steinberg::owned(host.adapter->createView(Vst::ViewType::kEditor));
    check(view != nullptr);

    auto native = MakeASound::VST3::PlugView::nativeViewType();
    check(view->isPlatformTypeSupported(native) == Steinberg::kResultTrue);
    check(view->isPlatformTypeSupported("Other") != Steinberg::kResultTrue);

    auto expected = MakeASound::GenericEditor(host.adapter->plugin()).initialSize();
    auto size = Steinberg::ViewRect {};
    check(view->getSize(&size) == Steinberg::kResultOk);
    check(size.getWidth() == expected.width);
    check(size.getHeight() == expected.height);
    check(view->canResize() == Steinberg::kResultTrue);

    view = nullptr;
    check(host.adapter->createView("other") == nullptr);
};
#endif
} // namespace
