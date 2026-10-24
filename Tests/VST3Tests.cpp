// The VST3 adapter hosted in-process: the factory, buses and arrangements, a
// whole block, parameter info and values, the automation queue, the edit gate,
// state through the component, MIDI in and out, CC through IMidiMapping, latency
// reporting and the editor view with its size policy.

#include "VST3TestHost.h"
#include "JuceFixtures.h"
#include "JucePort.h"
#include "TamberPlugFixtures.h"

#include <MakeASound/Plugin/VST3/Factory.h>
#include <MakeASound/Plugin/VST3/Text.h>

#include <pluginterfaces/gui/iplugview.h>
#include <pluginterfaces/vst/ivstchannelcontextinfo.h>
#include <pluginterfaces/vst/ivsthostapplication.h>
#include <pluginterfaces/vst/ivstmidicontrollers.h>

#if MAKEASOUND_VST3_HAS_VIEW
#include <MakeASound/Plugin/UI/GenericEditor.h>
#include <MakeASound/Plugin/VST3/PlugView.h>
#include "TestEditor.h"
#include <eacp/Graphics/Graphics/Keyboard.h>
#endif

#include <NanoTest/NanoTest.h>

#include <eacp/Core/Threads/EventLoop.h>

#include <climits>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <thread>

namespace VST3Tests
{
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
    check(answers(Vst::ChannelContext::IInfoListener::iid));

    auto unknown = Steinberg::FUID(1, 2, 3, 4);
    auto* none = static_cast<void*>(nullptr);
    check(factory->createInstance(
              reinterpret_cast<Steinberg::FIDString>(unknown.toTUID()),
              Vst::IComponent::iid,
              &none)
          == Steinberg::kNoInterface);
    check(none == nullptr);
};

auto tFactoryFormat = test("VST3/createInstanceSetsTheProcessFormat") = []
{
    MakeASound::setCurrentPluginFormat(MakeASound::PluginFormat::Unknown);

    // What the plugin's creation saw, before any wrapper existed.
    auto seenInConstructor = MakeASound::PluginFormat::Unknown;

    auto module = ModuleDescription {};
    module.vendor = "MakeASound Tests";
    module.manufacturerCode = "MaSo";
    module.plugins.add({.name = "Gain",
                        .pluginCode = "Gain",
                        .create = [&]
                        {
                            seenInConstructor = MakeASound::currentPluginFormat();
                            return EA::makeOwned<GainPlugin>();
                        }});

    auto factory = Steinberg::owned(MakeASound::VST3::makeFactory(module));
    auto cid = MakeASound::VST3::classIdFor(module, module.plugins[0]);

    auto* object = static_cast<void*>(nullptr);
    check(
        factory->createInstance(reinterpret_cast<Steinberg::FIDString>(cid.toTUID()),
                                Vst::IComponent::iid,
                                &object)
        == Steinberg::kResultOk);

    auto component = Steinberg::owned(static_cast<Vst::IComponent*>(object));
    check(seenInConstructor == MakeASound::PluginFormat::VST3);
    check(MakeASound::currentPluginFormat() == MakeASound::PluginFormat::VST3);
};

auto tOffline = test("VST3/anOfflineSetupReachesTheBlockAsOffline") = []
{
    auto host = AdapterHost<GainPlugin> {};

    host.fillBlock(blockSize);
    host.run();
    check(!host.plugin().offline);

    host.configure(48000.0, blockSize, Vst::kOffline);
    host.fillBlock(blockSize);
    host.run();
    check(host.plugin().offline);

    host.configure(48000.0, blockSize, Vst::kRealtime);
    host.fillBlock(blockSize);
    host.run();
    check(!host.plugin().offline);
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

// A main input, a sidechain left off until routed, an aux on, and MIDI buses
// one of each.
struct InactiveBusPlugin : GainPlugin
{
    BusLayout getBusLayout() const override
    {
        auto layout = BusLayout::stereoInOut();
        layout.inputs.add({.name = "Sidechain",
                           .numChannels = 1,
                           .isMain = false,
                           .activeByDefault = false});
        layout.inputs.add({.name = "Aux", .numChannels = 2, .isMain = false});
        layout.midiInputs.add({.name = "MIDI"});
        layout.midiOutputs.add({.name = "MIDI Out", .activeByDefault = false});
        return layout;
    }

    bool acceptsLayout(const BusLayout&) const override { return true; }
};

auto tDefaultActive = test("VST3/busesSayWhetherTheyStartActive") = []
{
    auto host = AdapterHost<InactiveBusPlugin> {};
    auto& adapter = *host.adapter;

    auto flagsOf =
        [&adapter](Vst::MediaType type, Vst::BusDirection direction, int i)
    {
        auto info = Vst::BusInfo {};
        check(adapter.getBusInfo(type, direction, i, info) == Steinberg::kResultOk);
        return info.flags;
    };

    check(adapter.getBusCount(Vst::kAudio, Vst::kInput) == 3);
    check(flagsOf(Vst::kAudio, Vst::kInput, 0) == Vst::BusInfo::kDefaultActive);
    check(flagsOf(Vst::kAudio, Vst::kInput, 1) == 0);
    check(flagsOf(Vst::kAudio, Vst::kInput, 2) == Vst::BusInfo::kDefaultActive);
    check(flagsOf(Vst::kEvent, Vst::kInput, 0) == Vst::BusInfo::kDefaultActive);
    check(flagsOf(Vst::kEvent, Vst::kOutput, 0) == 0);
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

auto tFractionalRate = test("VST3/aFractionalRateReachesThePluginUnrounded") = []
{
    auto host = AdapterHost<GainPlugin> {};
    auto& plugin = host.plugin();

    host.configure(44055.94, blockSize);
    check(plugin.spec.sampleRate == 44055.94);
    check(plugin.spec.roundedSampleRate() == 44056);

    host.configure(48047.5, blockSize);
    check(plugin.spec.sampleRate == 48047.5);
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

// A JUCE host (pluginval among them) hands an inactive bus its channel count with
// every channel pointer null (juce_VST3PluginFormat_test.cpp). The plugin sees
// silent scratch there, the active buses are untouched, and nothing is written
// to the host through a null pointer.
auto tNullChannels =
    test("VST3/nullChannelPointersOfAnInactiveBusAreSilentScratch") = []
{
    auto host = AdapterHost<AbsentChannelsPlugin> {};
    auto& plugin = host.plugin();
    auto nulls = std::vector<float*>(2, nullptr);

    for (auto ch = 0; ch < 2; ++ch)
        for (auto s = 0; s < blockSize; ++s)
            host.inputs[0].storage[ch][s] = 0.01f * static_cast<float>(s + ch);

    auto expected = host.inputs[0].storage;

    // Sidechain and Aux 1 inactive; Aux 2 active.
    host.fillBlock(blockSize);
    host.inputBuses[1].channelBuffers32 = nulls.data();
    host.outputBuses[1].channelBuffers32 = nulls.data();
    check(host.run() == Steinberg::kResultOk);

    check(plugin.processed == 1);
    check(plugin.nullChannels == 0);
    check(plugin.narrowBuses == 0);
    check(plugin.loudSidechainSamples == 0);
    check(host.outputs[0].storage == expected);

    for (const auto& channel: host.outputs[2].storage)
        check(std::all_of(
            channel.begin(), channel.end(), [](auto s) { return s == 3.f; }));

    check(nulls[0] == nullptr && nulls[1] == nullptr);

    // One null channel among real ones, on a shorter block: the real one is
    // written, and only for the block's length.
    for (auto& channel: host.outputs[2].storage)
        std::fill(channel.begin(), channel.end(), 0.f);

    auto mixed = std::vector<float*> {host.outputs[2].pointers[0], nullptr};
    host.fillBlock(blockSize / 2);
    host.inputBuses[1].channelBuffers32 = nulls.data();
    host.outputBuses[1].channelBuffers32 = nulls.data();
    host.outputBuses[2].channelBuffers32 = mixed.data();
    check(host.run() == Steinberg::kResultOk);

    check(plugin.processed == 2);
    check(plugin.nullChannels == 0);
    check(plugin.narrowBuses == 0);
    check(plugin.loudSidechainSamples == 0);

    const auto& aux = host.outputs[2].storage;
    check(std::all_of(aux[0].begin(),
                      aux[0].begin() + blockSize / 2,
                      [](auto s) { return s == 3.f; }));
    check(std::all_of(aux[0].begin() + blockSize / 2,
                      aux[0].end(),
                      [](auto s) { return s == 0.f; }));
    check(
        std::all_of(aux[1].begin(), aux[1].end(), [](auto s) { return s == 0.f; }));

    // No table at all for the inactive buses: the same as null pointers.
    host.fillBlock(blockSize);
    host.inputBuses[1].channelBuffers32 = nullptr;
    host.outputBuses[1].channelBuffers32 = nullptr;
    check(host.run() == Steinberg::kResultOk);

    check(plugin.processed == 3);
    check(plugin.nullChannels == 0);
    check(plugin.narrowBuses == 0);
    check(host.outputs[0].storage == expected);
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

    auto instrument = AdapterHost<SynthPlugin> {{}, withMidiControllers};
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

// A JUCE build's session, through the component as a host hands it over.

using JuceFixtures::FixturePlugin;

void writeBytes(Steinberg::IBStream& stream, const std::string& bytes)
{
    auto written = int32 {0};
    stream.write(
        const_cast<char*>(bytes.data()), static_cast<int32>(bytes.size()), &written);
    rewind(stream);
}

// Answers ISizeableStream with a size of its own and, when asked, every read
// with kResultFalse while still delivering, as Wavelab does.
struct QuirkyStream final
    : Steinberg::MemoryStream
    , Steinberg::ISizeableStream
{
    tresult PLUGIN_API read(void* buffer,
                            int32 numBytes,
                            int32* numBytesRead) override
    {
        auto result = Steinberg::MemoryStream::read(buffer, numBytes, numBytesRead);
        return failingReads ? Steinberg::kResultFalse : result;
    }

    tresult PLUGIN_API getStreamSize(Steinberg::int64& size) override
    {
        size = reportedSize;
        return Steinberg::kResultOk;
    }

    tresult PLUGIN_API setStreamSize(Steinberg::int64) override
    {
        return Steinberg::kResultFalse;
    }

    tresult PLUGIN_API queryInterface(const Steinberg::TUID _iid,
                                      void** obj) override
    {
        QUERY_INTERFACE(
            _iid, obj, Steinberg::ISizeableStream::iid, Steinberg::ISizeableStream)
        return Steinberg::MemoryStream::queryInterface(_iid, obj);
    }

    Steinberg::uint32 PLUGIN_API addRef() override
    {
        return Steinberg::MemoryStream::addRef();
    }

    Steinberg::uint32 PLUGIN_API release() override
    {
        return Steinberg::MemoryStream::release();
    }

    bool failingReads = false;
    Steinberg::int64 reportedSize = 0;
};

// Plug's VST3 getState writes its document with no framing: it loads as a native
// one, and loadLegacy never hears of it.
auto tPlugState = test("VST3/aTamberPlugSessionLoadsAsANativeDocument") = []
{
    auto svf = AdapterHost<TamberPlugFixtures::SVFPort> {};
    auto stream = Steinberg::MemoryStream {};
    writeBytes(stream, std::string {TamberPlugFixtures::svfDocument});

    check(svf.adapter->setState(&stream) == Steinberg::kResultOk);
    check(svf.plugin().legacyLoads == 0);
    check(TamberPlugFixtures::holdsSVFValues(svf.plugin().params));

    auto chorus = AdapterHost<TamberPlugFixtures::ChorusPort> {};
    auto chorusStream = Steinberg::MemoryStream {};
    writeBytes(chorusStream, std::string {TamberPlugFixtures::chorusDocument});

    check(chorus.adapter->setState(&chorusStream) == Steinberg::kResultOk);
    check(chorus.plugin().legacyLoads == 0);
    check(TamberPlugFixtures::holdsChorusValues(chorus.plugin().params));
};

auto tJuceState = test("VST3/aJuceSessionLoadsThroughThePluginsReader") = []
{
    auto host = AdapterHost<FixturePlugin> {};
    auto stream = Steinberg::MemoryStream {};
    writeBytes(stream,
               JuceFixtures::withTrailer(
                   JuceFixtures::blob(),
                   JuceFixtures::bytesOf(JuceFixtures::bypassedTrailer)));

    check(host.adapter->setState(&stream) == Steinberg::kResultOk);

    auto& plugin = host.plugin();
    check(plugin.legacyLoads == 1);
    check(plugin.origin == ForeignOrigin::JuceVST3);
    check(plugin.context == StateContext::Session);
    check(JuceFixtures::holdsFixtureValues(plugin.params));

    // The trailer's bypass is the one the wrapper added, as JUCE's was.
    check(plugin.bypass == std::optional {true});
    check(JuceFixtures::addedBypassOf(plugin) == 1.f);
    check(host.handler.restartsWith(Vst::kParamValuesChanged) == 1);
    check(host.handler.dirtyCalls == 0);

    // The next save is the native document, and it loads as one.
    auto saved = Steinberg::MemoryStream {};
    check(host.adapter->getState(&saved) == Steinberg::kResultOk);
    check(streamText(saved).starts_with("{"));
    rewind(saved);

    auto target = AdapterHost<FixturePlugin> {};
    target.adapter->setState(&saved);
    check(target.plugin().legacyLoads == 0);
    check(JuceFixtures::holdsFixtureValues(target.plugin().params));
    check(JuceFixtures::addedBypassOf(target.plugin()) == 1.f);
};

// A JUCE plugin whose own getStateInformation wrote JSON: its wrapper's trailer
// still marks it a JUCE build's, and the JSON reaches the plugin's reader, not
// the native load.
auto tJuceJson = test("VST3/aJuceSessionWhoseOwnStateIsJsonReachesTheReader") = []
{
    auto host = AdapterHost<FixturePlugin> {};
    auto json = std::string {"{\"cutoff\": 440, \"mode\": 2}"};
    auto stream = Steinberg::MemoryStream {};
    writeBytes(stream,
               JuceFixtures::withTrailer(
                   json, JuceFixtures::bytesOf(JuceFixtures::bypassedTrailer)));

    check(host.adapter->setState(&stream) == Steinberg::kResultOk);

    auto& plugin = host.plugin();
    check(plugin.legacyLoads == 1);
    check(plugin.origin == ForeignOrigin::JuceVST3);
    check(plugin.bytes == json);
    check(plugin.bypass == std::optional {true});

    // The fixture's reader takes only APVTS state, so nothing moved.
    check(JuceFixtures::holdsDefaults(plugin.params));
};

auto tJuceVst2Bank = test("VST3/aJuceVst2BankLoadsItsChunk") = []
{
    auto host = AdapterHost<FixturePlugin> {};
    auto stream = Steinberg::MemoryStream {};
    auto chunk = JuceFixtures::withTrailer(
        JuceFixtures::blob(), JuceFixtures::bytesOf(JuceFixtures::emptyTrailer));
    writeBytes(stream, JuceFixtures::chunkBank(chunk, true, 3));

    host.adapter->setState(&stream);

    auto& plugin = host.plugin();
    check(plugin.origin == ForeignOrigin::JuceVST2Bank);
    check(plugin.program == std::optional {3});
    check(JuceFixtures::holdsFixtureValues(plugin.params));

    // No private tree in the trailer: the 'VstW' header's bypass.
    check(JuceFixtures::addedBypassOf(plugin) == 1.f);
};

auto tJuceRefused = test("VST3/aJuceSessionThePluginRefusesMovesNothing") = []
{
    auto host = AdapterHost<FixturePlugin> {};
    host.plugin().refuse = true;

    auto stream = Steinberg::MemoryStream {};
    writeBytes(stream,
               JuceFixtures::withTrailer(
                   JuceFixtures::blob(),
                   JuceFixtures::bytesOf(JuceFixtures::bypassedTrailer)));

    check(host.adapter->setState(&stream) == Steinberg::kResultOk);
    check(host.plugin().legacyLoads == 1);
    check(JuceFixtures::holdsDefaults(host.plugin().params));
    check(JuceFixtures::addedBypassOf(host.plugin()) == 0.f);

    // Bytes that are neither ours nor JUCE's go to the native load.
    auto garbage = Steinberg::MemoryStream {};
    writeBytes(garbage, "not a document");
    host.adapter->setState(&garbage);
    check(host.plugin().legacyLoads == 1);
    check(JuceFixtures::holdsDefaults(host.plugin().params));
};

auto tJuceOffThread =
    test("VST3/aJuceSessionFromAWorkerLoadsOnTheMessageThread") = []
{
    auto host = AdapterHost<FixturePlugin> {};
    auto stream = Steinberg::MemoryStream {};
    writeBytes(stream,
               JuceFixtures::withTrailer(
                   JuceFixtures::blob(),
                   JuceFixtures::bytesOf(JuceFixtures::emptyTrailer)));

    auto loader = std::thread([&] { host.adapter->setState(&stream); });
    loader.join();

    check(host.plugin().legacyLoads == 0);
    check(pumpUntil([&] { return host.plugin().legacyLoads == 1; }));
    check(JuceFixtures::holdsFixtureValues(host.plugin().params));
    check(pumpUntil(
        [&] { return host.handler.restartsWith(Vst::kParamValuesChanged) >= 2; }));
};

// The identity a description names (WP10.19, WP10.20): the class id and every
// ParamID, MakeASound's unchanged and a JUCE build's for a port.

// JUCE's numbers, as JUCE 8.0.12 printed them (Tests/JuceIdentityTests.cpp).
constexpr auto juceCutoffMasked = Vst::ParamID {798419949u};
constexpr auto juceCutoffUnmasked = Vst::ParamID {2945903597u};
constexpr auto juceBypass = Vst::ParamID {0x62797073u}; // 'byps'

// JUCE's masked id for a key: String::hashCode & 0x7fffffff.
Vst::ParamID juceId(std::string_view key)
{
    return MakeASound::Compat::juceStringHash(key) & 0x7fffffffu;
}

// The fixture's cutoff (20..20000, skew 0.3) at 440, where its state puts it.
const auto fixtureCutoffNormalized =
    static_cast<double>(JuceFixtures::cutoffRange.toNormalized(440.f));

// JUCE 8.0.12's NormalisableRange<float>::convertFrom0to1 and snapToLegalValue
// (juce_NormalisableRange.h) for the fixture's cutoff, step for step: the plain
// value a JUCE build's AudioParameterFloat::setValue stores for a lane's value.
// The real anchor is the pinned constants (216.665802f, 7678.31152f) in
// VST3/aJuceLaneLandsOnTheJuceBuildsPlainValue, computed by running JUCE
// 8.0.12's own arithmetic; this helper documents the formula.
float juceCutoffFrom0to1(float proportion)
{
    auto start = 20.f;
    auto end = 20000.f;
    auto skew = 0.3f;
    proportion = proportion < 0.f ? 0.f : (proportion > 1.f ? 1.f : proportion);

    if (skew != 1.f && proportion > 0.f)
        proportion = std::exp(std::log(proportion) / skew);

    auto value = start + (end - start) * proportion;
    return (value <= start || end <= start) ? start : (value >= end ? end : value);
}

const ModuleDescription& juceModule()
{
    static const auto module = []
    {
        auto description = ModuleDescription {};
        description.vendor = "MakeASound Tests";
        description.manufacturerCode = "MaSo";

        auto create = [] { return EA::makeOwned<FixturePlugin>(); };

        // JUCE's default, JUCE_VST3_CAN_REPLACE_VST2 on.
        description.plugins.add({.name = "Juce Compat Fixture",
                                 .pluginCode = "Jcmp",
                                 .create = create,
                                 .idScheme = IdScheme::JUCE});

        // Off: JUCE's own component id.
        description.plugins.add({.name = "Juce Compat Fixture",
                                 .pluginCode = "Jcmp",
                                 .create = create,
                                 .idScheme = IdScheme::JUCE,
                                 .juce = {.vst3CanReplaceVst2 = false}});
        return description;
    }();

    return module;
}

Steinberg::FUID classIdAt(Steinberg::IPluginFactory2& factory, int index)
{
    auto info = Steinberg::PClassInfo2 {};
    factory.getClassInfo2(index, &info);
    return Steinberg::FUID::fromTUID(info.cid);
}

bool hasWords(const Steinberg::FUID& id, const MakeASound::Vst3ClassId& words)
{
    return id.getLong1() == words[0] && id.getLong2() == words[1]
           && id.getLong3() == words[2] && id.getLong4() == words[3];
}

std::optional<Vst::ParameterInfo> infoFor(Vst::IEditController& controller,
                                          Vst::ParamID id)
{
    for (auto i = 0; i < controller.getParameterCount(); ++i)
    {
        auto info = Vst::ParameterInfo {};

        if (controller.getParameterInfo(i, info) == Steinberg::kResultOk
            && info.id == id)
            return info;
    }

    return std::nullopt;
}

bool flagged(const Vst::ParameterInfo& info, int32 flag)
{
    return (info.flags & flag) != 0;
}

auto tNativeClassId = test("VST3/theMakeASoundClassIdIsUnchanged") = []
{
    const auto& module = testModule();
    auto factory = Steinberg::owned(MakeASound::VST3::makeFactory(module));

    // FUID('MaSo', <plugin code>, 'VST3', 0), as every shipped build had it.
    check(hasWords(classIdAt(*factory, 0),
                   {0x4d61536fu, 0x4761696eu, 0x56535433u, 0u}));
    check(hasWords(classIdAt(*factory, 1),
                   {0x4d61536fu, 0x53796e74u, 0x56535433u, 0u}));
};

auto tJuceClassIds = test("VST3/aJucePortsClassIdIsTheJuceBuilds") = []
{
    const auto& module = juceModule();
    auto factory = Steinberg::owned(MakeASound::VST3::makeFactory(module));

    check(factory->countClasses() == 2);

    // Under JUCE_VST3_CAN_REPLACE_VST2 the component is the VST2 plugin's id,
    // 'VST' + 'Jcmp' + "juce comp", which is what has a host replace the VST2.
    check(hasWords(classIdAt(*factory, 0),
                   {0x5653544au, 0x636d706au, 0x75636520u, 0x636f6d70u}));

    // Without it, FUID(0xABCDEF01, 0x9182FAEB, 'MaSo', 'Jcmp').
    check(hasWords(classIdAt(*factory, 1),
                   {0xabcdef01u, 0x9182faebu, 0x4d61536fu, 0x4a636d70u}));
};

// Through the factory as a host does: a JUCE build's session loads, and its
// automation lanes (the ids a host saved beside it) address the same parameters.
auto tJuceSessionIds = test("VST3/aJuceSessionKeepsItsAutomationIds") = []
{
    const auto& module = juceModule();
    auto factory = Steinberg::owned(MakeASound::VST3::makeFactory(module));
    auto cid = classIdAt(*factory, 0);

    auto* object = static_cast<void*>(nullptr);
    check(
        factory->createInstance(reinterpret_cast<Steinberg::FIDString>(cid.toTUID()),
                                Vst::IComponent::iid,
                                &object)
        == Steinberg::kResultOk);

    auto component = Steinberg::owned(static_cast<Vst::IComponent*>(object));
    auto controller = Steinberg::FUnknownPtr<Vst::IEditController>(component);
    check(controller != nullptr);
    check(component->initialize(nullptr) == Steinberg::kResultOk);

    auto cutoff = infoFor(*controller, juceCutoffMasked);
    check(cutoff.has_value());
    check(cutoff && flagged(*cutoff, Vst::ParameterInfo::kCanAutomate));

    auto bypass = infoFor(*controller, juceBypass);
    check(bypass.has_value());
    check(bypass && flagged(*bypass, Vst::ParameterInfo::kIsBypass));

    // Every other fixture parameter under JUCE's hash of its id.
    for (const auto* key: {"mode", "enabled", "voices", "attack", "extra"})
        check(infoFor(*controller, juceId(key)).has_value());

    auto stream = Steinberg::MemoryStream {};
    writeBytes(stream,
               JuceFixtures::withTrailer(
                   JuceFixtures::blob(),
                   JuceFixtures::bytesOf(JuceFixtures::bypassedTrailer)));
    check(component->setState(&stream) == Steinberg::kResultOk);

    check(near(controller->getParamNormalized(juceCutoffMasked),
               fixtureCutoffNormalized));
    check(controller->getParamNormalized(juceBypass) == 1.0);

    // A lane on JUCE's id moves the parameter it moved in the JUCE build.
    check(controller->setParamNormalized(juceCutoffMasked, 0.25)
          == Steinberg::kResultTrue);
    check(near(controller->getParamNormalized(juceCutoffMasked), 0.25));
    check(controller->setParamNormalized(juceBypass, 0.0)
          == Steinberg::kResultTrue);
    check(controller->getParamNormalized(juceBypass) == 0.0);

    component->terminate();
};

// A lane's normalized value, through the process queue and through the
// controller, lands on the plain value the JUCE build set for it: the cutoff's
// skewed curve, the int's rounding.
auto tJuceLaneValues = test("VST3/aJuceLaneLandsOnTheJuceBuildsPlainValue") = []
{
    auto host =
        AdapterHost<FixturePlugin> {policyFor(IdScheme::JUCE, {}).parameters};
    auto& params = host.plugin().params;

    // JUCE 8.0.12 on macOS: convertFrom0to1(0.25f) is 216.665802f. The pinned
    // constants, computed by running JUCE's own arithmetic, are the real anchor;
    // juceCutoffFrom0to1 documents the formula. Another libm may round exp and
    // log an ulp apart, so the exact check is against that formula run here.
    auto juceAtQuarter = juceCutoffFrom0to1(0.25f);
    check(std::abs(juceAtQuarter - 216.665802f) <= 216.665802f * 1e-6f);

    host.addPoint(juceCutoffMasked, 0, 0.25);
    host.addPoint(juceId("voices"), 0, 0.63);
    host.fillBlock(blockSize);
    host.run();
    check(params.cutoff.get() == juceAtQuarter);

    // AudioParameterInt: 1 + 0.63 * 15 = 10.45, rounded.
    check(params.voices.get() == 10);

    check(host.adapter->setParamNormalized(juceCutoffMasked, 0.75)
          == Steinberg::kResultTrue);
    check(params.cutoff.get() == juceCutoffFrom0to1(0.75f));
    check(std::abs(params.cutoff.get() - 7678.31152f) <= 7678.31152f * 1e-6f);
};

// JUCE publishes a non-automatable parameter, without kCanAutomate; MakeASound's
// scheme keeps hiding it.
auto tJuceNonAutomatable =
    test("VST3/aJucePortListsItsNonAutomatableParameters") = []
{
    auto host =
        AdapterHost<LatencyPlugin> {policyFor(IdScheme::JUCE, {}).parameters};
    auto& adapter = *host.adapter;
    auto hiddenId = juceId("Hidden");

    check(adapter.getParameterCount() == 3);

    auto hidden = infoFor(adapter, hiddenId);
    check(hidden.has_value());
    check(hidden && !flagged(*hidden, Vst::ParameterInfo::kCanAutomate));
    check(hidden && !flagged(*hidden, Vst::ParameterInfo::kIsHidden));

    auto latency = infoFor(adapter, juceId("Latency"));
    check(latency && flagged(*latency, Vst::ParameterInfo::kCanAutomate));

    auto bypass = infoFor(adapter, juceBypass);
    check(bypass && flagged(*bypass, Vst::ParameterInfo::kIsBypass));
    check(bypass && flagged(*bypass, Vst::ParameterInfo::kCanAutomate));

    // The host may set and read it, and the editor's gesture on it reaches the
    // host, as JUCE's does.
    auto& param = host.plugin().params.hidden;
    check(adapter.setParamNormalized(hiddenId, 0.75) == Steinberg::kResultTrue);
    check(near(param.getNormalized(), 0.75));
    check(near(adapter.getParamNormalized(hiddenId), 0.75));

    auto index = host.plugin().parameters().indexOf(&param);
    auto& listener = *host.plugin().hostEditListener();
    listener.beginParameterEdit(index);
    param.setNormalized(0.5f);
    listener.performParameterEdit(index, 0.5f);
    listener.endParameterEdit(index);

    const auto& edits = host.handler.edits;
    check(edits.size() == 3);
    check(edits.size() == 3 && edits[1].id == hiddenId
          && near(edits[1].value, 0.5));

    auto native = AdapterHost<LatencyPlugin> {};
    check(native.adapter->getParameterCount() == 2);
    check(!infoFor(*native.adapter, native.hostIdOf(native.plugin().params.hidden))
               .has_value());
};

// JUCE_USE_STUDIO_ONE_COMPATIBLE_PARAMETERS off: ids of 2^31 and more, through
// the automation queue, the controller and the plugin's own moves.
auto tJuceUnmasked = test("VST3/aJucePortsUnmaskedIdsReachThePlugin") = []
{
    auto host = AdapterHost<FixturePlugin> {
        policyFor(IdScheme::JUCE, {.studioOneCompatibleParameters = false})
            .parameters};
    auto& cutoff = host.plugin().params.cutoff;

    check(host.hostIdOf(cutoff) == juceCutoffUnmasked);
    check(infoFor(*host.adapter, juceCutoffUnmasked).has_value());

    // The bypass stays 'byps'.
    check(infoFor(*host.adapter, juceBypass).has_value());

    host.addPoint(juceCutoffUnmasked, 0, 0.5);
    host.fillBlock(blockSize);
    host.run();
    check(near(cutoff.getNormalized(), 0.5));
    check(near(host.adapter->getParamNormalized(juceCutoffUnmasked), 0.5));

    host.clearQueues();
    cutoff.setNormalized(0.125f);
    host.fillBlock(blockSize);
    host.run();

    auto points = outputPoints(host.outputChanges, juceCutoffUnmasked);
    check(points.size() == 1);
    check(points.size() == 1 && near(points[0].second, 0.125));
};

// JUCE_FORCE_USE_LEGACY_PARAM_IDS: every parameter by its index, the bypass the
// next one.
auto tJuceLegacy = test("VST3/aJuceLegacyPortAddressesParametersByIndex") = []
{
    auto host = AdapterHost<FixturePlugin> {
        policyFor(IdScheme::JUCELegacyIndex, {}).parameters};
    auto& adapter = *host.adapter;
    const auto& list = host.plugin().parameters();

    check(adapter.getParameterCount() == list.size());

    for (auto i = 0; i < list.size(); ++i)
    {
        auto info = Vst::ParameterInfo {};
        adapter.getParameterInfo(i, info);
        check(info.id == static_cast<Vst::ParamID>(i));
    }

    auto bypass = infoFor(adapter, static_cast<Vst::ParamID>(list.size() - 1));
    check(bypass && flagged(*bypass, Vst::ParameterInfo::kIsBypass));

    // Index 0 is the cutoff.
    host.addPoint(0, 0, 0.75);
    host.fillBlock(blockSize);
    host.run();
    check(near(host.plugin().params.cutoff.getNormalized(), 0.75));
};

auto tQuirkyStream =
    test("VST3/aStreamThatMisreportsItsSizeAndStatusStillLoads") = []
{
    auto source = AdapterHost<GainPlugin> {};
    source.plugin().params.gain.setValue(-6.f);

    // Long enough to take several reads.
    source.plugin().state.preset = std::string(10000, 'p');
    source.plugin().state.markChanged();

    auto saved = Steinberg::MemoryStream {};
    source.adapter->getState(&saved);

    auto quirky = QuirkyStream {};
    quirky.failingReads = true;
    quirky.reportedSize = 3;
    writeBytes(quirky, streamText(saved));

    auto target = AdapterHost<GainPlugin> {};
    target.adapter->setState(&quirky);
    check(near(target.plugin().params.gain.getValue(), -6.f));
    check(pumpUntil([&] { return target.plugin().state.preset.size() == 10000; }));

    // A size past any reservation is only a hint too.
    quirky.failingReads = false;
    quirky.reportedSize = std::numeric_limits<Steinberg::int64>::max();
    rewind(quirky);
    auto other = AdapterHost<GainPlugin> {};
    other.adapter->setState(&quirky);
    check(near(other.plugin().params.gain.getValue(), -6.f));
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
    auto host = AdapterHost<SynthPlugin> {{}, withMidiControllers};
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
    auto host = AdapterHost<SynthPlugin> {{}, withMidiControllers};
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
    auto host = AdapterHost<SynthPlugin> {{}, withMidiControllers};
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

    auto host = AdapterHost<SynthPlugin> {{}, withMidiControllers};
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

// A module of one plugin, P under `category` and `midiControllers`, with the
// adapter its factory creates, initialized as a host would.
template <typename P>
struct FactoryHosted
{
    FactoryHosted(Category category, std::optional<bool> midiControllers)
    {
        module.vendor = "MakeASound Tests";
        module.manufacturerCode = "MaSo";
        module.plugins.add({.name = "Hosted",
                            .category = category,
                            .pluginCode = "Hstd",
                            .create = [] { return EA::makeOwned<P>(); },
                            .vst3MidiControllers = midiControllers});

        auto factory = Steinberg::owned(MakeASound::VST3::makeFactory(module));
        auto cid = MakeASound::VST3::classIdFor(module, module.plugins[0]);
        auto* object = static_cast<void*>(nullptr);
        factory->createInstance(reinterpret_cast<Steinberg::FIDString>(cid.toTUID()),
                                Vst::IComponent::iid,
                                &object);

        component = Steinberg::owned(static_cast<Vst::IComponent*>(object));
        component->initialize(nullptr);
    }

    ~FactoryHosted() { component->terminate(); }

    Adapter& adapter() { return *static_cast<Adapter*>(component.get()); }

    ModuleDescription module;
    Steinberg::IPtr<Vst::IComponent> component;
};

// The plugin's registered parameters come first, by the same host ids whatever
// the setting; the 2096 hidden shadows of its one MIDI bus follow only when
// MIDI controllers are on, and the mapping answers only then.
template <typename P>
void checkMidiControllers(Category category,
                          std::optional<bool> midiControllers,
                          bool expectOn)
{
    using MakeASound::VST3::decodeShadowId;

    auto hosted = FactoryHosted<P> {category, midiControllers};
    auto& adapter = hosted.adapter();
    const auto& list = adapter.plugin().parameters();

    auto pluginIds = std::vector<Vst::ParamID> {};

    for (auto i = 0; i < list.size(); ++i)
        if (list.isRegistered(i))
            pluginIds.push_back(list.entry(i).hostId);

    auto numPluginIds = static_cast<int>(pluginIds.size());
    check(numPluginIds > 0);
    check(adapter.getParameterCount() == numPluginIds + (expectOn ? 2096 : 0));

    for (auto i = 0; i < adapter.getParameterCount(); ++i)
    {
        auto info = Vst::ParameterInfo {};
        check(adapter.getParameterInfo(i, info) == Steinberg::kResultOk);

        if (i < numPluginIds)
        {
            check(info.id == pluginIds[static_cast<size_t>(i)]);
            check(!decodeShadowId(info.id, 1).has_value());
        }
        else
        {
            check(decodeShadowId(info.id, 1).has_value());
            check(flagged(info, Vst::ParameterInfo::kIsHidden));
        }
    }

    auto mapping = Steinberg::FUnknownPtr<Vst::IMidiMapping>(hosted.component);
    check(mapping != nullptr);

    auto id = Vst::ParamID {};
    auto expected = expectOn ? Steinberg::kResultTrue : Steinberg::kResultFalse;
    check(mapping->getMidiControllerAssignment(0, 0, 1, id) == expected);
    check(mapping->getMidiControllerAssignment(0, 0, Vst::kPitchBend, id)
          == expected);
}

auto tMidiControllersResolve =
    test("VST3/midiControllersFollowTheCategoryUnlessSet") = []
{
    auto resolved = [](Category category, std::optional<bool> setting)
    {
        return MakeASound::vst3MidiControllersFor(
            {.category = category, .vst3MidiControllers = setting});
    };

    check(resolved(Category::Instrument, {}));
    check(!resolved(Category::Effect, {}));
    check(!resolved(Category::MidiEffect, {}));

    check(resolved(Category::Effect, true));
    check(resolved(Category::MidiEffect, true));
    check(!resolved(Category::Instrument, false));
    check(resolved(Category::Instrument, true));
    check(!resolved(Category::Effect, false));
};

auto tInstrumentControllers =
    test("VST3/anInstrumentMapsMidiControllersByDefault") = []
{ checkMidiControllers<SynthPlugin>(Category::Instrument, {}, true); };

auto tEffectControllers =
    test("VST3/anEffectWithMidiInMapsNoMidiControllersByDefault") = []
{
    checkMidiControllers<LatentEffect>(Category::Effect, {}, false);
    checkMidiControllers<LatentEffect>(Category::MidiEffect, {}, false);
};

auto tEffectAsksForControllers = test("VST3/anEffectCanAskForMidiControllers") = []
{ checkMidiControllers<LatentEffect>(Category::Effect, true, true); };

auto tInstrumentDeclinesControllers =
    test("VST3/anInstrumentCanTurnMidiControllersOff") = []
{ checkMidiControllers<SynthPlugin>(Category::Instrument, false, false); };

// Off, a point on a would-be shadow id is not a parameter and is dropped, while
// notes and a controller the host sends as an event still reach the plugin.
auto tWithoutControllers = test("VST3/withoutMidiControllersOnlyEventsArrive") = []
{
    auto host = AdapterHost<SynthPlugin> {};
    auto id = Vst::ParamID {};

    check(host.adapter->getMidiControllerAssignment(0, 3, 74, id)
          == Steinberg::kResultFalse);

    auto legacy = Vst::Event {};
    legacy.type = Vst::Event::kLegacyMIDICCOutEvent;
    legacy.sampleOffset = 9;
    legacy.midiCCOut.channel = 1;
    legacy.midiCCOut.controlNumber = 7;
    legacy.midiCCOut.value = 127;

    host.addPoint(MakeASound::VST3::shadowIdFor(0, 3, 74), 0, 0.5);
    host.addEvent(noteEvent(true, 5));
    host.addEvent(legacy);
    host.fillBlock(blockSize);
    host.run();

    const auto& plugin = host.plugin();
    check(plugin.numEvents == 2);
    check(plugin.events[0].isNoteOn() && plugin.events[0].sampleOffset == 5);

    const auto* cc = plugin.events[1].asControlChange();
    check(cc != nullptr && cc->controller == 7 && near(cc->value, 1.0));
    check(plugin.events[1].sampleOffset == 9);
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

// Renames its parameters at run time, with one the host never sees between the
// two it does.
struct RetitlingPlugin : Plugin
{
    struct Param : FloatParam
    {
        using FloatParam::FloatParam;
        using FloatParam::setDisplayInfo;
    };

    struct Params : ParameterGroup
    {
        Params() { add(first, internal, second); }

        Param first {"Gain", 0.f, 1.f, 0.5f, {.shortName = "Gn"}};
        Param internal {"Internal", 0.f, 1.f, 0.f, {.automatable = false}};
        Param second {"Pan", 0.f, 1.f, 0.5f};
    };

    RetitlingPlugin() { setParameters(ParameterList {params}); }

    std::string_view name() const override { return "Retitling"; }
    void prepare(const ProcessSpec&) override {}
    void process(ProcessContext&) noexcept override {}

    void retitle()
    {
        params.first.setDisplayInfo("Volume", {}, {});
        params.second.setDisplayInfo("Balance", {}, {});
        refreshParameterNames();
    }

    Params params;
};

// The host's view, by position in the controller's list rather than by id.
Vst::ParameterInfo infoAt(Adapter& adapter, int32 index)
{
    auto info = Vst::ParameterInfo {};
    check(adapter.getParameterInfo(index, info) == Steinberg::kResultOk);
    return info;
}

auto tRetitle = test("VST3/aRenameRewritesTheTitlesButNoId") = []
{
    using MakeASound::VST3::toUtf8;

    auto host = AdapterHost<RetitlingPlugin> {};
    auto& adapter = *host.adapter;
    auto firstId = host.hostIdOf(host.plugin().params.first);
    auto secondId = host.hostIdOf(host.plugin().params.second);

    check(toUtf8(infoAt(adapter, 0).title) == "Gain");
    check(toUtf8(infoAt(adapter, 0).shortTitle) == "Gn");
    check(infoAt(adapter, 1).id == secondId);

    host.handler.restarts.clear();
    host.plugin().retitle();

    check(host.handler.restarts.size() == 1);
    check(host.handler.restartsWith(Vst::kParamTitlesChanged) == 1);

    check(toUtf8(infoAt(adapter, 0).title) == "Volume");
    check(toUtf8(infoAt(adapter, 0).shortTitle).empty());
    check(infoAt(adapter, 0).id == firstId);
    check(toUtf8(infoAt(adapter, 1).title) == "Balance");
    check(infoAt(adapter, 1).id == secondId);

    // The state keys stay the ones it was built with.
    check(adapter.plugin().parameters().entry(0).id == "Gain");
};

auto tRetitleUnhandled =
    test("VST3/aRenameWithoutAHandlerIsReadBackWithoutARestart") = []
{
    auto host = AdapterHost<RetitlingPlugin> {};
    host.adapter->setComponentHandler(nullptr);
    host.handler.restarts.clear();

    host.plugin().retitle();

    check(host.handler.restarts.empty());
    check(MakeASound::VST3::toUtf8(infoAt(*host.adapter, 0).title) == "Volume");
};

// The channel context a host hands IInfoListener: strings and integers by key.
// Stack-owned, so its reference count is a no-op.
struct TestChannelContext final : Vst::IAttributeList
{
    tresult PLUGIN_API setInt(AttrID id, Steinberg::int64 value) override
    {
        ints[id] = value;
        return Steinberg::kResultTrue;
    }

    tresult PLUGIN_API getInt(AttrID id, Steinberg::int64& value) override
    {
        auto found = ints.find(id);

        if (found == ints.end())
            return Steinberg::kResultFalse;

        value = found->second;
        return Steinberg::kResultTrue;
    }

    tresult PLUGIN_API setFloat(AttrID, double) override
    {
        return Steinberg::kResultFalse;
    }

    tresult PLUGIN_API getFloat(AttrID, double&) override
    {
        return Steinberg::kResultFalse;
    }

    tresult PLUGIN_API setString(AttrID id, const Vst::TChar* text) override
    {
        strings[id] = MakeASound::VST3::toUtf8(text);
        return Steinberg::kResultTrue;
    }

    tresult PLUGIN_API getString(AttrID id,
                                 Vst::TChar* text,
                                 Steinberg::uint32 sizeInBytes) override
    {
        auto found = strings.find(id);

        if (found == strings.end() || sizeInBytes < sizeof(Vst::String128))
            return Steinberg::kResultFalse;

        MakeASound::VST3::copyTo(text, found->second);
        return Steinberg::kResultTrue;
    }

    tresult PLUGIN_API setBinary(AttrID, const void*, Steinberg::uint32) override
    {
        return Steinberg::kResultFalse;
    }

    tresult PLUGIN_API getBinary(AttrID, const void*&, Steinberg::uint32&) override
    {
        return Steinberg::kResultFalse;
    }

    tresult PLUGIN_API queryInterface(const Steinberg::TUID _iid,
                                      void** obj) override
    {
        QUERY_INTERFACE(_iid, obj, Steinberg::FUnknown::iid, Vst::IAttributeList)
        QUERY_INTERFACE(_iid, obj, Vst::IAttributeList::iid, Vst::IAttributeList)
        *obj = nullptr;
        return Steinberg::kNoInterface;
    }

    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }

    std::map<std::string, std::string> strings;
    std::map<std::string, Steinberg::int64> ints;
};

auto tChannelContext = test("VST3/theChannelContextReachesTheTrackInfo") = []
{
    namespace CC = Vst::ChannelContext;

    auto host = AdapterHost<GainPlugin> {};
    auto& plugin = host.plugin();

    auto* listener = static_cast<void*>(nullptr);
    check(host.adapter->queryInterface(CC::IInfoListener::iid, &listener)
          == Steinberg::kResultOk);
    auto* info = static_cast<CC::IInfoListener*>(listener);

    auto context = TestChannelContext {};
    context.strings[CC::kChannelNameKey] = "Lead Vocal";
    context.strings[CC::kChannelUIDKey] = "track-7";
    context.strings[CC::kChannelIndexNamespaceKey] = "Audio";
    context.ints[CC::kChannelIndexKey] = 4;
    context.ints[CC::kChannelIndexNamespaceOrderKey] = 2;
    // ARGB.
    context.ints[CC::kChannelColorKey] = 0xff102030;
    context.ints[CC::kChannelPluginLocationKey] = CC::kPostVolumeFader;

    check(info->setChannelContextInfos(&context) == Steinberg::kResultOk);
    info->release();

    const auto& track = plugin.trackInfo();
    check(plugin.trackInfoChanges == 1);
    check(track.isValid);
    check(track.name == "Lead Vocal");
    check(track.uid == "track-7");
    check(track.indexNamespace == "Audio");
    check(track.index == 4);
    check(track.namespaceOrder == 2);
    check(track.color == MakeASound::TrackColor {0x10, 0x20, 0x30, 0xff});
    check(track.location == MakeASound::PluginLocation::PostVolumeFader);

    // A rename alone: what the host leaves out goes back to its default.
    auto renamed = TestChannelContext {};
    renamed.strings[CC::kChannelNameKey] = "Lead Vox";
    check(host.adapter->setChannelContextInfos(&renamed) == Steinberg::kResultOk);
    check(plugin.trackInfoChanges == 2);
    check(plugin.trackInfo().name == "Lead Vox");
    check(plugin.trackInfo().index == 0);
    check(plugin.trackInfo().location == MakeASound::PluginLocation::Unknown);

    check(host.adapter->setChannelContextInfos(nullptr)
          == Steinberg::kInvalidArgument);
    check(plugin.trackInfoChanges == 2);
};

// The host context initialize() receives, naming the host. Stack-owned.
struct TestHostApplication final : Vst::IHostApplication
{
    tresult PLUGIN_API getName(Vst::String128 name) override
    {
        MakeASound::VST3::copyTo(name, hostName);
        return Steinberg::kResultOk;
    }

    tresult PLUGIN_API createInstance(Steinberg::TUID,
                                      Steinberg::TUID,
                                      void** obj) override
    {
        *obj = nullptr;
        return Steinberg::kResultFalse;
    }

    tresult PLUGIN_API queryInterface(const Steinberg::TUID _iid,
                                      void** obj) override
    {
        QUERY_INTERFACE(_iid, obj, Steinberg::FUnknown::iid, Vst::IHostApplication)
        QUERY_INTERFACE(_iid, obj, Vst::IHostApplication::iid, Vst::IHostApplication)
        *obj = nullptr;
        return Steinberg::kNoInterface;
    }

    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }

    std::string hostName;
};

auto tHostName = test("VST3/initializeTellsThePluginTheHostName") = []
{
    auto adapter = Steinberg::owned(new Adapter(EA::makeOwned<GainPlugin>()));
    check(adapter->plugin().hostName().empty());

    // UTF-8 through the host's UTF-16 and back.
    auto application = TestHostApplication {};
    application.hostName = "Live \xc3\xbc";
    check(adapter->initialize(&application) == Steinberg::kResultOk);
    check(adapter->plugin().hostName() == "Live \xc3\xbc");
    adapter->terminate();

    // A host with no IHostApplication leaves it empty.
    auto unnamed = Steinberg::owned(new Adapter(EA::makeOwned<GainPlugin>()));
    check(unnamed->initialize(nullptr) == Steinberg::kResultOk);
    check(unnamed->plugin().hostName().empty());
    unnamed->terminate();
};

// One block of `numSamples` with nothing from the host, whose output changes the
// cases read.
void runQuietBlock(AdapterHost<GainPlugin>& host, int numSamples = blockSize)
{
    host.clearQueues();
    host.fillBlock(numSamples);
    host.run();
}

auto tPluginMoves = test("VST3/aPluginSideMoveLeavesAnOutputPoint") = []
{
    auto host = AdapterHost<GainPlugin> {};
    auto& params = host.plugin().params;
    auto gainId = host.hostIdOf(params.gain);
    auto modeId = host.hostIdOf(params.mode);

    runQuietBlock(host);
    check(host.outputChanges.getParameterCount() == 0);

    params.gain.setValue(-7.f);
    runQuietBlock(host);

    auto points = outputPoints(host.outputChanges, gainId);
    check(points.size() == 1);
    check(points[0].first == 0);
    check(near(points[0].second, params.gain.getNormalized()));
    check(host.outputChanges.getParameterCount() == 1);

    runQuietBlock(host);
    check(host.outputChanges.getParameterCount() == 0);

    // A zero-sample flush carries it too.
    params.mode.setValue(1.f);
    runQuietBlock(host, 0);
    check(outputPoints(host.outputChanges, modeId).size() == 1);
};

auto tHostWritesNotEchoed = test("VST3/aHostWriteIsNotEchoedBack") = []
{
    auto host = AdapterHost<GainPlugin> {};
    auto gainId = host.hostIdOf(host.plugin().params.gain);

    host.clearQueues();
    host.addPoint(gainId, 0, 0.3);
    host.fillBlock(blockSize);
    host.run();
    check(host.outputChanges.getParameterCount() == 0);

    host.adapter->setParamNormalized(gainId, 0.6);
    runQuietBlock(host);
    check(host.outputChanges.getParameterCount() == 0);

    // A choice snaps the host's value; the snapped form is not sent back either.
    auto modeId = host.hostIdOf(host.plugin().params.mode);
    host.adapter->setParamNormalized(modeId, 0.4);
    runQuietBlock(host);
    check(host.outputChanges.getParameterCount() == 0);
};

auto tHeldNoOutput = test("VST3/aHeldParameterLeavesNoOutputPoint") = []
{
    auto host = AdapterHost<GainPlugin> {};
    auto& gain = host.plugin().params.gain;
    auto& listener = *host.plugin().hostEditListener();

    listener.beginParameterEdit(0);
    gain.setValue(-20.f);
    runQuietBlock(host);
    check(host.outputChanges.getParameterCount() == 0);

    listener.performParameterEdit(0, gain.getNormalized());
    gain.setValue(-25.f);
    runQuietBlock(host);
    check(host.outputChanges.getParameterCount() == 0);

    listener.performParameterEdit(0, gain.getNormalized());
    listener.endParameterEdit(0);
    runQuietBlock(host);
    check(host.outputChanges.getParameterCount() == 0);
};

auto tBulkValues = test("VST3/manyValuesChangedRestartsParamValues") = []
{
    auto host = AdapterHost<GainPlugin> {};
    moveEveryParameter(host.plugin().params);
    host.plugin().notifyHostParameterValuesChanged();

    check(host.handler.restartsWith(Vst::kParamValuesChanged) == 1);

    // The host re-reads every value, so no block repeats them.
    runQuietBlock(host);
    check(host.outputChanges.getParameterCount() == 0);
};

auto tSetStateRestarts = test("VST3/setStateRestartsParamValues") = []
{
    auto source = AdapterHost<GainPlugin> {};
    moveEveryParameter(source.plugin().params);

    auto stream = Steinberg::MemoryStream {};
    source.adapter->getState(&stream);
    rewind(stream);

    auto target = AdapterHost<GainPlugin> {};
    check(target.adapter->setState(&stream) == Steinberg::kResultOk);
    check(target.handler.restartsWith(Vst::kParamValuesChanged) == 1);

    runQuietBlock(target);
    check(target.outputChanges.getParameterCount() == 0);

    // From a worker the restart is marshalled to the message thread.
    rewind(stream);
    auto loader = std::thread([&] { target.adapter->setState(&stream); });
    loader.join();

    check(pumpUntil(
        [&] { return target.handler.restartsWith(Vst::kParamValuesChanged) == 2; }));
};

// A project another release of the plugin saved. NewerGainPlugin is GainPlugin
// with `drive` added, so each one's session is the other's across an upgrade or
// a downgrade. The host hands the blob back through setState and replays the
// lanes by the ids the saving release published, some of which this one never
// had.

// The newer release's session, every parameter moved, as its getState wrote it.
std::string newerSession()
{
    auto source = AdapterHost<NewerGainPlugin> {};
    auto& params = source.plugin().params;
    params.gain.setValue(-12.f);
    params.drive.setValue(0.9f);
    params.filter.cutoff.setValue(440.f);
    params.mode.setValue(2.f);
    params.width.setValue(0.9f);

    auto stream = Steinberg::MemoryStream {};
    source.adapter->getState(&stream);
    return streamText(stream);
}

// The newer release's drive, which GainPlugin does not have.
Vst::ParamID removedDriveId()
{
    auto newer = AdapterHost<NewerGainPlugin> {};
    return newer.hostIdOf(newer.plugin().params.drive);
}

// An id no release of the plugin ever published.
constexpr auto neverPublishedId = Vst::ParamID {12345};

tresult loadSession(Adapter& adapter, const std::string& bytes)
{
    auto stream = Steinberg::MemoryStream {};
    writeBytes(stream, bytes);
    return adapter.setState(&stream);
}

std::vector<float> valuesOf(const MakeASound::ParameterList& list)
{
    auto values = std::vector<float> {};

    for (auto i = 0; i < list.size(); ++i)
        values.push_back(list[i].getValue());

    return values;
}

auto tNewerSession = test("VST3/aNewerReleasesSessionLoadsWhatThisOneHas") = []
{
    auto host = AdapterHost<GainPlugin> {};
    check(loadSession(*host.adapter, newerSession()) == Steinberg::kResultOk);

    const auto& params = host.plugin().params;
    check(near(params.gain.getValue(), -12.f));
    check(near(params.filter.cutoff.getValue(), 440.f, 1e-2));
    check(params.mode.getIndex() == 2);
    check(near(params.width.getValue(), 0.9f));
    check(host.plugin().parameters().indexOfHostId(removedDriveId()) < 0);

    // What the host repopulates its panels and lanes from.
    check(near(host.adapter->getParamNormalized(host.hostIdOf(params.gain)),
               params.gain.toNormalized(-12.f)));
    check(host.handler.restartsWith(Vst::kParamValuesChanged) == 1);
};

auto tOlderSession =
    test("VST3/anOlderReleasesSessionRestoresAddedParametersDefaults") = []
{
    auto source = AdapterHost<GainPlugin> {};
    moveEveryParameter(source.plugin().params);

    auto stream = Steinberg::MemoryStream {};
    source.adapter->getState(&stream);

    auto host = AdapterHost<NewerGainPlugin> {};
    auto& params = host.plugin().params;
    params.drive.setValue(0.6f);
    check(loadSession(*host.adapter, streamText(stream)) == Steinberg::kResultOk);

    check(near(params.gain.getValue(), -12.f));
    check(near(params.filter.cutoff.getValue(), 440.f, 1e-2));
    check(params.mode.getIndex() == 2);
    check(params.bypass.isOn());
    check(near(params.width.getValue(), 0.9f));

    // The session never heard of drive, so it is back at its default, and the
    // host reads that.
    check(near(params.drive.getValue(), 0.25f));
    check(near(host.adapter->getParamNormalized(host.hostIdOf(params.drive)),
               params.drive.toNormalized(0.25f)));
};

auto tRemovedLanes = test("VST3/aLaneForARemovedParameterIsRefused") = []
{
    auto host = AdapterHost<GainPlugin> {};
    auto& adapter = *host.adapter;
    const auto& list = adapter.plugin().parameters();
    auto removed = removedDriveId();
    check(list.indexOfHostId(neverPublishedId) < 0);

    auto before = valuesOf(list);

    check(adapter.setParamNormalized(removed, 0.9) == Steinberg::kResultFalse);
    check(adapter.setParamNormalized(neverPublishedId, 0.9)
          == Steinberg::kResultFalse);
    check(valuesOf(list) == before);

    // Nothing moved, so the next block reports nothing back.
    runQuietBlock(host);
    check(host.outputChanges.getParameterCount() == 0);

    auto& gain = host.plugin().params.gain;
    check(adapter.setParamNormalized(host.hostIdOf(gain), 1.0)
          == Steinberg::kResultTrue);
    check(near(gain.getNormalized(), 1.0));
};

auto tRemovedAutomation =
    test("VST3/automationForARemovedParameterIsIgnoredInProcess") = []
{
    using MakeASound::VST3::shadowIdFor;

    auto host = AdapterHost<GainPlugin> {};
    const auto& list = host.adapter->plugin().parameters();
    auto& gain = host.plugin().params.gain;
    auto gainIndex = list.indexOf(&gain);
    auto removed = removedDriveId();

    auto expected = valuesOf(list);
    expected[static_cast<size_t>(gainIndex)] = gain.toPlain(1.f);

    host.clearQueues();
    host.addPoint(host.hostIdOf(gain), 0, 1.0);
    host.addPoint(removed, 0, 0.9);
    host.addPoint(neverPublishedId, 0, 0.9);
    // A MIDI controller lane from a release that mapped them: this one maps none.
    host.addPoint(shadowIdFor(3, 0, 7), 0, 0.5);
    host.fillBlock(blockSize);

    check(host.run() == Steinberg::kResultOk);
    check(host.plugin().processed == 1);
    check(near(gain.getNormalized(), 1.0));
    check(valuesOf(list) == expected);

    // One for a MIDI bus this instrument does not have carries no event.
    auto synth = AdapterHost<SynthPlugin> {{}, withMidiControllers};
    synth.addPoint(shadowIdFor(3, 0, 7), 0, 0.5);
    synth.fillBlock(blockSize);

    check(synth.run() == Steinberg::kResultOk);
    check(synth.plugin().numEvents == 0);
};

auto tNewerProject =
    test("VST3/aNewerProjectsStateLanesAndAutomationRestoreTogether") = []
{
    auto host = AdapterHost<GainPlugin> {};
    auto& adapter = *host.adapter;
    auto& params = host.plugin().params;
    auto gainId = host.hostIdOf(params.gain);
    auto cutoffId = host.hostIdOf(params.filter.cutoff);
    auto removed = removedDriveId();

    // The host recalls the state...
    check(loadSession(adapter, newerSession()) == Steinberg::kResultOk);

    // ...replays the lane values it recorded, by the newer release's ids...
    adapter.setParamNormalized(gainId, params.gain.toNormalized(-12.f));
    adapter.setParamNormalized(cutoffId, params.filter.cutoff.toNormalized(440.f));
    check(adapter.setParamNormalized(removed, 0.7) == Steinberg::kResultFalse);

    // ...and plays the recorded automation.
    host.clearQueues();
    host.addPoint(gainId, 0, 0.25);
    host.addPoint(removed, 0, 0.9);
    host.fillBlock(blockSize);
    check(host.run() == Steinberg::kResultOk);

    check(near(params.gain.getNormalized(), 0.25));
    check(near(params.filter.cutoff.getValue(), 440.f, 1e-2));
    check(params.mode.getIndex() == 2);
    check(near(params.width.getValue(), 0.9f));
};

auto tDirty = test("VST3/markChangedSetsTheHostDirty") = []
{
    auto host = AdapterHost<GainPlugin> {};

    host.plugin().state.preset = "Edited";
    host.plugin().state.markChanged();
    check(host.handler.dirtyCalls == 1);

    // A parameter alone is the host's to notice, and a load the host's own doing.
    host.plugin().params.gain.setValue(-3.f);
    check(host.handler.dirtyCalls == 1);

    auto other = AdapterHost<GainPlugin> {};
    other.plugin().state.preset = "Other";
    other.plugin().state.markChanged();

    auto stream = Steinberg::MemoryStream {};
    other.adapter->getState(&stream);
    rewind(stream);
    host.adapter->setState(&stream);

    check(pumpUntil([&] { return host.plugin().state.preset == "Other"; }));
    check(host.handler.dirtyCalls == 1);
};

#if MAKEASOUND_VST3_HAS_VIEW
auto tView = test("VST3/theEditorViewIsTheGenericEditor") = []
{
    auto host = AdapterHost<GainPlugin> {};
    auto view = Steinberg::owned(host.adapter->createView(Vst::ViewType::kEditor));
    check(view != nullptr);
    Steinberg::FUnknownPtr<Steinberg::IPlugViewContentScaleSupport>(view.get())
        ->setContentScaleFactor(1.f);

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

// Points the test plugins' createEditor() at a TestEditor for its lifetime.
struct ScopedTestEditor
{
    explicit ScopedTestEditor(EditorConstraints limits = {},
                              EditorSize size = {400, 300})
    {
        Module::editorFactory = [this, limits, size](Plugin&)
        {
            auto created = EA::makeOwned<TestEditor>();
            created->limits = limits;
            created->size = size;
            editor = created.get();
            return OwningPointer<Editor>(std::move(created));
        };
    }

    ~ScopedTestEditor() { Module::editorFactory = nullptr; }

    ScopedTestEditor(const ScopedTestEditor&) = delete;
    ScopedTestEditor& operator=(const ScopedTestEditor&) = delete;

    TestEditor* editor = nullptr;
};

EditorConstraints bounded()
{
    return {.resizable = true, .minimum = {200, 100}, .maximum = {800, 600}};
}

EditorConstraints ratioLocked()
{
    return {.resizable = true,
            .minimum = {150, 100},
            .maximum = {1500, 1000},
            .aspectRatio = {3, 2}};
}

struct EditorView
{
    // At a content scale of 1, as a host would set it: unscaled, a Windows view
    // follows the machine's DPI (VST3ViewTests-Windows.cpp).
    explicit EditorView(AdapterHost<GainPlugin>& host)
        : view(Steinberg::owned(host.adapter->createView(Vst::ViewType::kEditor)))
    {
        Steinberg::FUnknownPtr<Steinberg::IPlugViewContentScaleSupport>(view.get())
            ->setContentScaleFactor(1.f);
    }

    Steinberg::ViewRect size()
    {
        auto rect = Steinberg::ViewRect {};
        view->getSize(&rect);
        return rect;
    }

    Steinberg::ViewRect constrained(Steinberg::ViewRect rect)
    {
        check(view->checkSizeConstraint(&rect) == Steinberg::kResultTrue);
        return rect;
    }

    Steinberg::IPtr<Steinberg::IPlugView> view;
};

bool sameSize(const Steinberg::ViewRect& rect, int width, int height)
{
    return rect.getWidth() == width && rect.getHeight() == height;
}

auto tCanResize = test("VST3/canResizeFollowsTheEditorsConstraints") = []
{
    auto host = AdapterHost<GainPlugin> {};

    {
        auto fixed = ScopedTestEditor {};
        auto view = EditorView(host);
        check(view.view->canResize() == Steinberg::kResultFalse);

        auto rect = Steinberg::ViewRect {0, 0, 900, 900};
        check(view.view->checkSizeConstraint(&rect) != Steinberg::kResultTrue);
    }

    auto resizable = ScopedTestEditor {bounded(), {1000, 50}};
    auto view = EditorView(host);
    check(view.view->canResize() == Steinberg::kResultTrue);
    check(sameSize(view.size(), 800, 100));
};

auto tCheckSize = test("VST3/checkSizeConstraintAppliesTheEditorsPolicy") = []
{
    auto host = AdapterHost<GainPlugin> {};

    for (auto contentScale: {1.f, 2.f})
    {
        auto bounds = ScopedTestEditor {bounded()};
        auto boundsView = EditorView(host);
        auto scaling =
            Steinberg::FUnknownPtr<Steinberg::IPlugViewContentScaleSupport>(
                boundsView.view.get());
        check(scaling->setContentScaleFactor(contentScale) == Steinberg::kResultOk);

        // Pixels per point in a ViewRect: 1 on macOS whatever the scale.
        auto factor = boundsView.size().getWidth() / 400;
        auto px = [factor](int points) { return points * factor; };

        auto large = boundsView.constrained({0, 0, px(1000), px(50)});
        check(sameSize(large, px(800), px(100)));
        check(sameSize(boundsView.constrained(large), px(800), px(100)));

        auto offset = boundsView.constrained({10, 20, 10 + px(500), 20 + px(400)});
        check(offset.left == 10 && offset.top == 20);
        check(sameSize(offset, px(500), px(400)));

        auto ratio = ScopedTestEditor {ratioLocked(), {600, 400}};
        auto ratioView = EditorView(host);
        Steinberg::FUnknownPtr<Steinberg::IPlugViewContentScaleSupport>(
            ratioView.view.get())
            ->setContentScaleFactor(contentScale);

        auto skewed = ratioView.constrained({0, 0, px(600), px(600)});
        check(std::abs(skewed.getWidth() * 2 - skewed.getHeight() * 3)
              <= 3 * factor);
        check(sameSize(
            ratioView.constrained(skewed), skewed.getWidth(), skewed.getHeight()));
        check(sameSize(
            ratioView.constrained({0, 0, px(900), px(600)}), px(900), px(600)));
    }
};

auto tNoFrame = test("VST3/requestResizeWithoutAFrameIsRefused") = []
{
    auto host = AdapterHost<GainPlugin> {};
    auto scoped = ScopedTestEditor {bounded()};
    auto view = EditorView(host);

    check(!scoped.editor->requestResize({500, 500}));
    check(sameSize(view.size(), 400, 300));
};

auto tFrame = test("VST3/requestResizeGoesThroughThePlugFrame") = []
{
    auto host = AdapterHost<GainPlugin> {};
    auto scoped = ScopedTestEditor {bounded()};
    auto view = EditorView(host);
    auto frame = FakePlugFrame {};
    check(view.view->setFrame(&frame) == Steinberg::kResultOk);

    check(scoped.editor->requestResize({1000, 450}));
    check(frame.requests.size() == 1);
    check(sameSize(frame.requests.back(), 800, 450));
    check(sameSize(view.size(), 800, 450));

    frame.callOnSize = false;
    check(scoped.editor->requestResize({300, 50}));
    check(sameSize(frame.requests.back(), 300, 100));
    check(sameSize(view.size(), 300, 100));

    frame.accept = false;
    check(!scoped.editor->requestResize({600, 600}));
    check(frame.requests.size() == 3);
    check(sameSize(view.size(), 300, 100));

    view.view->setFrame(nullptr);
};

// Records whether the view had cleared the host by the time it destroyed us.
struct TeardownEditor final : Editor
{
    explicit TeardownEditor(bool& clearedToSet)
        : cleared(clearedToSet)
    {
    }

    ~TeardownEditor() override { cleared = host() == nullptr; }

    eacp::Graphics::View& view() override { return inner.view(); }

    bool& cleared;
    TestEditor inner;
};

auto tHost = test("VST3/theViewIsTheEditorsHostForItsLifetime") = []
{
    auto host = AdapterHost<GainPlugin> {};

    {
        auto scoped = ScopedTestEditor {bounded()};
        auto view = EditorView(host);
        auto* plugView = static_cast<MakeASound::VST3::PlugView*>(view.view.get());

        check(scoped.editor->host() == static_cast<EditorHost*>(plugView));
        check(scoped.editor->host()->drawsResizeHandle());
        check(scoped.editor->attached == 0);
    }

    auto cleared = false;
    Module::editorFactory = [&cleared](Plugin&)
    { return OwningPointer<Editor>(EA::makeOwned<TeardownEditor>(cleared)); };

    auto view = EditorView(host);
    Module::editorFactory = nullptr;
    view.view = nullptr;
    check(cleared);
};

// Not attached, there is no host window to hand a key to on any platform, so
// the editor's own route keeps it. Windows' attached case: VST3ViewTests-Windows.
auto tKeyUnattached = test("VST3/anUnattachedViewClaimsNoUnhandledKey") = []
{
    auto host = AdapterHost<GainPlugin> {};
    auto scoped = ScopedTestEditor {bounded()};
    auto view = EditorView(host);

    auto key = eacp::Graphics::KeyEvent {};
    key.keyCode = eacp::Graphics::KeyCode::Space;

    check(scoped.editor->host() != nullptr);
    check(!scoped.editor->forwardUnhandledKey(key));
};
#endif

// An effect with no bypass parameter: the wrapper's is the one kIsBypass entry,
// and the host's bypass reaches it.
auto tAddedBypass = test("VST3/theWrappersBypassIsTheHostsBypass") = []
{
    auto host = AdapterHost<UnbypassedGainPlugin> {};
    auto& adapter = *host.adapter;
    auto bypasses = 0;
    auto bypassId = Vst::ParamID {};

    for (auto i = 0; i < adapter.getParameterCount(); ++i)
    {
        auto info = Vst::ParameterInfo {};
        adapter.getParameterInfo(i, info);

        if ((info.flags & Vst::ParameterInfo::kIsBypass) != 0)
        {
            ++bypasses;
            bypassId = info.id;
        }
    }

    check(bypasses == 1);
    check(bypassId == MakeASound::hostIdFor("__bypass"));

    host.plugin().params.gain.setValue(6.f);

    for (auto& channel: host.inputs[0].storage)
        std::fill(channel.begin(), channel.end(), 0.5f);

    host.addPoint(bypassId, 0, 1.0);

    // Past the 480-sample crossfade.
    for (auto b = 0; b < 10; ++b)
    {
        host.fillBlock(blockSize);
        host.run();
        host.clearQueues();
    }

    check(adapter.getParamNormalized(bypassId) == 1.0);

    for (auto& channel: host.outputs[0].storage)
        for (auto s = 0; s < blockSize; ++s)
            check(channel[static_cast<size_t>(s)] == 0.5f);
};

// An instrument: JUCE's wrapper gives it 'byps' too, so a JUCE port's does; under
// MakeASound's scheme an instrument still gets no bypass.
auto tInstrumentBypass = test("VST3/aJuceInstrumentHasTheWrappersBypassAsByps") = []
{
    auto bypassesOf = [](Adapter& adapter, Vst::ParamID& bypassId)
    {
        auto bypasses = 0;

        for (auto i = 0; i < adapter.getParameterCount(); ++i)
        {
            auto info = Vst::ParameterInfo {};
            adapter.getParameterInfo(i, info);

            if ((info.flags & Vst::ParameterInfo::kIsBypass) != 0)
            {
                ++bypasses;
                bypassId = info.id;
            }
        }

        return bypasses;
    };

    auto native = AdapterHost<SynthPlugin> {};
    auto unused = Vst::ParamID {};
    check(bypassesOf(*native.adapter, unused) == 0);

    auto ported = AdapterHost<SynthPlugin> {
        MakeASound::policyFor(MakeASound::IdScheme::JUCE, {}).parameters};
    auto& adapter = *ported.adapter;
    auto bypassId = Vst::ParamID {};
    check(bypassesOf(adapter, bypassId) == 1);
    check(bypassId == 0x62797073u); // 'byps'

    // One more than the native one's, whatever the MIDI controllers add.
    check(adapter.getParameterCount() == native.adapter->getParameterCount() + 1);

    ported.plugin().params.level.setValue(0.5f);
    ported.addPoint(bypassId, 0, 1.0);

    // Past the 480-sample crossfade: nothing to delay, so silence.
    for (auto b = 0; b < 10; ++b)
    {
        ported.fillBlock(blockSize);
        ported.run();
        ported.clearQueues();
    }

    check(adapter.getParamNormalized(bypassId) == 1.0);

    for (auto& channel: ported.outputs[0].storage)
        for (auto s = 0; s < blockSize; ++s)
            check(channel[static_cast<size_t>(s)] == 0.f);
};

// The latency follows a parameter: once the host's write has run the adapter's
// latency check, the bypassed output lines up with the new latency, with no
// prepare in between.
auto tAddedBypassLatency =
    test("VST3/theWrappersBypassFollowsALatencyParameter") = []
{
    auto host = AdapterHost<LatencyPlugin> {};
    auto& adapter = *host.adapter;
    const auto& list = adapter.plugin().parameters();
    auto bypassId = list.entry(list.bypassIndex()).hostId;
    auto latencyId = host.hostIdOf(host.plugin().params.latency);

    check(adapter.getLatencySamples() == 0);
    adapter.setParamNormalized(bypassId, 1.0);

    auto& input = host.inputs[0].storage[0];
    auto& output = host.outputs[0].storage[0];
    auto heard = std::vector<float> {};

    auto runBlock = [&](int index)
    {
        for (auto s = 0; s < blockSize; ++s)
            input[static_cast<size_t>(s)] =
                static_cast<float>(1 + index * blockSize + s);

        host.fillBlock(blockSize);
        host.run();
        heard.insert(heard.end(), output.begin(), output.begin() + blockSize);
    };

    // Past the crossfade into bypass, at no latency.
    for (auto b = 0; b < 10; ++b)
        runBlock(b);

    check(heard.back() == static_cast<float>(10 * blockSize));

    adapter.setParamNormalized(latencyId, 0.2);
    check(adapter.getLatencySamples() == 200);
    check(host.handler.restartsWith(Vst::kLatencyChanged) == 1);

    heard.clear();

    for (auto b = 0; b < 6; ++b)
        runBlock(10 + b);

    auto first = 1 + 10 * blockSize;

    for (auto k = 200; k < static_cast<int>(heard.size()); ++k)
        check(heard[static_cast<size_t>(k)] == static_cast<float>(first + k - 200));
};

// What Plug's own SVFFilter and Chorus VST3s published (TamberPlugFixtures.h),
// from a port under IdScheme::TamberPlug: the index ids, steps, defaults and
// kCanAutomate. MakeASound adds kIsList to a choice (a display hint Plug's
// RangeParameter lacked), and registers the bypass the wrapper adds after
// them, which Plug had none of.
template <typename P, size_t count>
void checkPlugsVST3(const TamberPlugFixtures::PlugParameter (&published)[count])
{
    auto identity =
        MakeASound::policyFor(MakeASound::IdScheme::TamberPlug, {}).parameters;
    auto host = AdapterHost<P> {identity};
    auto& adapter = *host.adapter;

    check(adapter.getParameterCount() == static_cast<int32>(count) + 1);

    for (auto index = 0; index < static_cast<int>(count); ++index)
    {
        auto info = Vst::ParameterInfo {};
        check(adapter.getParameterInfo(index, info) == Steinberg::kResultOk);

        const auto& plug = published[index];
        check(info.id == static_cast<Vst::ParamID>(index));
        check(MakeASound::VST3::toUtf8(info.title) == plug.name);
        check(info.stepCount == plug.vst3Steps);
        check(near(info.defaultNormalizedValue, plug.vst3Default, 1e-6));
        check(info.unitId == Vst::kRootUnitId);
        check((info.flags & ~Vst::ParameterInfo::kIsList)
              == Vst::ParameterInfo::kCanAutomate);
    }

    auto bypass = Vst::ParameterInfo {};
    adapter.getParameterInfo(static_cast<int32>(count), bypass);
    check(bypass.id == MakeASound::hostIdFor("__bypass"));
    check((bypass.flags & Vst::ParameterInfo::kIsBypass) != 0);
}

// What JUCE 8.0.12's own VST3 build of JucePort's twin published (JucePort.h),
// under each JUCE scheme. MakeASound differs only in kIsList on a choice. The
// steps are StepRule::JuceDiscrete's: an int has none, as JUCE's
// AudioParameterInt is not discrete to its VST3 wrapper.
void checkJucesVST3(MakeASound::IdScheme scheme)
{
    auto legacy = scheme == MakeASound::IdScheme::JUCELegacyIndex;
    auto host = AdapterHost<JucePort::PortPlugin> {
        MakeASound::policyFor(scheme, {}).parameters};
    auto& adapter = *host.adapter;

    constexpr auto count = static_cast<int>(std::size(JucePort::juceVST3));
    check(adapter.getParameterCount() == count);

    for (auto index = 0; index < count; ++index)
    {
        const auto& juce = JucePort::juceVST3[index];
        auto info = Vst::ParameterInfo {};
        check(adapter.getParameterInfo(index, info) == Steinberg::kResultOk);

        check(info.id == (legacy ? juce.legacyId : juce.id));
        check(near(info.defaultNormalizedValue, juce.defaultValue, 1e-6));
        check((info.flags & ~Vst::ParameterInfo::kIsList) == juce.flags);
        check(info.unitId
              == (std::string_view {juce.key} == "attack"
                      ? JucePort::juceEnvelopeUnit
                      : Vst::kRootUnitId));
        check(info.stepCount == juce.stepCount);
    }
}

// One parameter of each kind of steps, in registration order: a stepped float,
// an int, a choice, a bool, a continuous float (the gain), then the added bypass.
struct StepKindParams : ParameterGroup
{
    StepKindParams() { add(stepped, count, mode, enabled, gain); }

    static constexpr auto hint = ParameterOptions {.versionHint = 1};

    FloatParam stepped {"Stepped", {0.f, 1.f, 0.25f}, 0.5f, hint};
    IntParam count {"Count", 1, 8, 2, hint};
    ChoiceParam mode {"Mode", {"Clean", "Warm", "Hot"}, 0, hint};
    BoolParam enabled {"Enabled", true, hint};
    DecibelParam gain {"Gain", -60.f, 12.f, 0.f, hint};
};

// MakeASound's scheme publishes every stepped parameter's numSteps() as its
// stepCount; a JUCE scheme only a choice's and a bool's, as JUCE's VST3 does
// (Param::updateParameterInfo), the stepped float and the int 0.
auto tStepRules = test("VST3/stepCountFollowsTheStepRule") = []
{
    using Steps = std::vector<int>;

    auto stepsUnder = [](const MakeASound::ParameterIdentity& identity)
    {
        auto host = AdapterHost<GainPluginOf<StepKindParams>> {identity};
        auto& adapter = *host.adapter;
        auto steps = Steps {};

        for (auto index = 0; index < adapter.getParameterCount(); ++index)
        {
            auto info = Vst::ParameterInfo {};
            check(adapter.getParameterInfo(index, info) == Steinberg::kResultOk);
            steps.push_back(info.stepCount);
        }

        return steps;
    };

    check(stepsUnder({}) == Steps {4, 7, 2, 1, 0, 1});

    for (auto scheme:
         {MakeASound::IdScheme::JUCE, MakeASound::IdScheme::JUCELegacyIndex})
        check(stepsUnder(MakeASound::policyFor(scheme, {}).parameters)
              == Steps {0, 0, 2, 1, 0, 1});

    // JUCE_FORCE_LEGACY_PARAMETER_AUTOMATION_TYPE: every parameter's
    // getNumSteps() - 1, the int's included, and 0 for a continuous one. (JUCE's
    // AudioParameterFloat would answer 0 for the stepped float too, since it
    // ignores its interval; Compat::juceNumSteps.)
    auto legacyType = MakeASound::JuceSettings {};
    legacyType.forceLegacyParameterAutomationType = true;
    auto policy = MakeASound::policyFor(MakeASound::IdScheme::JUCE, legacyType);
    check(stepsUnder(policy.parameters) == Steps {4, 7, 2, 1, 0, 1});
};

auto tJuceGoldens = test("VST3/aJucePortPublishesWhatJucesVST3Did") = []
{
    checkJucesVST3(MakeASound::IdScheme::JUCE);
    checkJucesVST3(MakeASound::IdScheme::JUCELegacyIndex);

    auto port = JucePort::description(MakeASound::IdScheme::JUCE);
    port.juce.vst3CanReplaceVst2 = false;
    auto module = MakeASound::ModuleDescription {.manufacturerCode = "MaSo"};
    check(MakeASound::vst3ClassIdFor(module, port) == JucePort::juceVST3ClassId);
};

// IUnitInfo as a host asks for it, through queryInterface.
std::vector<Vst::UnitInfo> unitsOf(Adapter& adapter)
{
    auto units = std::vector<Vst::UnitInfo> {};
    auto* unitInfo = static_cast<Vst::IUnitInfo*>(nullptr);

    if (adapter.queryInterface(Vst::IUnitInfo::iid,
                               reinterpret_cast<void**>(&unitInfo))
        != Steinberg::kResultOk)
        return units;

    for (auto index = 0; index < unitInfo->getUnitCount(); ++index)
    {
        auto info = Vst::UnitInfo {};
        check(unitInfo->getUnitInfo(index, info) == Steinberg::kResultTrue);
        units.push_back(info);
    }

    unitInfo->release();
    return units;
}

Vst::ParameterInfo infoOf(Adapter& adapter, Vst::ParamID id)
{
    for (auto index = 0; index < adapter.getParameterCount(); ++index)
    {
        auto info = Vst::ParameterInfo {};
        adapter.getParameterInfo(index, info);

        if (info.id == id)
            return info;
    }

    return {};
}

// The JUCE port's one group is a unit under the root, as JUCE's.
auto tJuceUnits = test("VST3/aJucePortsGroupIsAUnitAsInJuce") = []
{
    auto host = AdapterHost<JucePort::PortPlugin> {
        MakeASound::policyFor(MakeASound::IdScheme::JUCE, {}).parameters};
    auto units = unitsOf(*host.adapter);

    check(units.size() == 2);
    check(units[0].id == Vst::kRootUnitId);
    check(units[0].parentUnitId == Vst::kNoParentUnitId);
    check(units[0].programListId == Vst::kNoProgramListId);
    check(MakeASound::VST3::toUtf8(units[0].name) == "Root Unit");
    check(units[1].id == JucePort::juceEnvelopeUnit);
    check(units[1].parentUnitId == Vst::kRootUnitId);
    check(MakeASound::VST3::toUtf8(units[1].name) == "Envelope");
};

// BitFuzzer's nested groups (TwinGroupPlugin) publish the units and unit ids
// its JUCE build did, the two "EQ" groups apart, every grouped parameter in its
// innermost group's unit and at the ids JUCE gave it.
auto tTwinUnits = test("VST3/nestedGroupsAreTheUnitsJuceGaveThem") = []
{
    const auto& description = JucePort::twinGroupsDescription();
    auto host =
        AdapterHost<TwinGroupPlugin> {MakeASound::parameterIdentityFor(description)};
    auto& adapter = *host.adapter;
    auto units = unitsOf(adapter);

    check(units.size() == 1 + std::size(JucePort::twinGroupsUnits));
    check(units[0].id == Vst::kRootUnitId);

    for (size_t index = 0;
         index < std::size(JucePort::twinGroupsUnits) && index + 1 < units.size();
         ++index)
    {
        const auto& juce = JucePort::twinGroupsUnits[index];
        const auto& ours = units[index + 1];
        check(ours.id == juce.id);
        check(ours.parentUnitId == juce.parent);
        check(ours.programListId == Vst::kNoProgramListId);
        check(MakeASound::VST3::toUtf8(ours.name) == juce.name);
    }

    for (const auto& juce: JucePort::twinGroupsParameters)
    {
        auto info = infoOf(adapter, juce.hostId);
        check(info.id == juce.hostId);
        check(info.unitId == juce.unit);
    }

    const auto& list = adapter.plugin().parameters();
    check(infoOf(adapter, list.entry(list.indexOf("In")).hostId).unitId
          == Vst::kRootUnitId);
    check(list.identityProblems().empty());
};

// Under MakeASound's ids a unit's id is FNV-1a of its group's path, and its
// parent the enclosing group's unit.
auto tNativeUnits = test("VST3/nativeGroupsAreUnitsByPath") = []
{
    auto host = AdapterHost<TwinGroupPlugin> {};
    auto& adapter = *host.adapter;
    auto units = unitsOf(adapter);

    check(units.size() == 5);

    if (units.size() != 5)
        return;

    auto drive = static_cast<Vst::UnitID>(MakeASound::hostIdFor("DriveFilter"));
    auto driveEQ = static_cast<Vst::UnitID>(MakeASound::hostIdFor("DriveFilter/EQ"));
    check(units[1].id == drive && units[1].parentUnitId == Vst::kRootUnitId);
    check(units[2].id == driveEQ && units[2].parentUnitId == drive);
    check(MakeASound::VST3::toUtf8(units[2].name) == "EQ");
    check(units[4].id
          == static_cast<Vst::UnitID>(MakeASound::hostIdFor("MasterFilter/EQ")));

    const auto& list = adapter.plugin().parameters();
    check(
        infoOf(adapter, list.entry(list.indexOf("DriveFilter/EQ/Low")).hostId).unitId
        == driveEQ);
};

// Tamber Plug's VST3 had no units: its ports publish none (hostGroups off).
auto tPlugUnits = test("VST3/aTamberPlugPortHasNoUnits") = []
{
    auto identity =
        MakeASound::policyFor(MakeASound::IdScheme::TamberPlug, {}).parameters;
    auto host = AdapterHost<TamberPlugFixtures::SVFPort> {identity};
    check(unitsOf(*host.adapter).empty());
};

// A JUCE port's MIDI controller parameters are JUCE's (JucePort.h): the
// mapping's answers, the count, the flags, the titles, under either id mode,
// and a point on one reaches the plugin as that controller.
auto tJuceControllers = test("VST3/aJucePortsMidiControllersAreJuces") = []
{
    for (auto scheme:
         {MakeASound::IdScheme::JUCE, MakeASound::IdScheme::JUCELegacyIndex})
    {
        auto host = AdapterHost<SynthPlugin> {
            MakeASound::policyFor(scheme, {}).parameters, withMidiControllers};
        auto& adapter = *host.adapter;

        for (const auto& juce: JucePort::juceMidiMappings)
        {
            auto id = Vst::ParamID {};
            check(adapter.getMidiControllerAssignment(
                      juce.bus,
                      static_cast<Steinberg::int16>(juce.channel),
                      static_cast<Vst::CtrlNumber>(juce.controller),
                      id)
                  == Steinberg::kResultTrue);
            check(id == juce.id);
        }

        // No program change slot.
        auto id = Vst::ParamID {};
        check(adapter.getMidiControllerAssignment(0, 0, Vst::kCtrlProgramChange, id)
              == Steinberg::kResultFalse);

        auto plugins = adapter.plugin().parameters().size();
        check(adapter.getParameterCount()
              == plugins + JucePort::juceMidiControllerCount);

        auto last = Vst::ParameterInfo {};
        check(adapter.getParameterInfo(adapter.getParameterCount() - 1, last)
              == Steinberg::kResultOk);
        check(last.id == 0x6D63751Fu);
        check(last.flags == 0 && last.stepCount == 0);
        check(last.defaultNormalizedValue == 0.0);
        check(MakeASound::VST3::toUtf8(last.title) == "MIDI CC 15|129");
    }

    auto host = AdapterHost<SynthPlugin> {
        MakeASound::policyFor(MakeASound::IdScheme::JUCE, {}).parameters,
        withMidiControllers};
    host.addPoint(0x6D636D00u + 3 * 130 + 74, 0, 0.5);
    host.fillBlock(blockSize);
    host.run();

    const auto& plugin = host.plugin();
    check(plugin.numEvents == 1);
    const auto* cc = plugin.events[0].asControlChange();
    check(cc != nullptr && cc->controller == 74 && plugin.events[0].channel == 3);
};

// MakeASound's layout at another base, here Plug's: Plug's ids for CC, bend and
// aftertouch (probed from Plug's Synth), plus a program change slot Plug had
// not.
auto tControllerBase = test("VST3/theNativeControllerLayoutTakesABase") = []
{
    auto identity =
        MakeASound::policyFor(MakeASound::IdScheme::TamberPlug, {}).parameters;
    identity.vst3Controllers = {MakeASound::Vst3ControllerLayout::Native,
                                0x80000000u};

    auto host = AdapterHost<SynthPlugin> {identity, withMidiControllers};
    auto& adapter = *host.adapter;
    auto id = Vst::ParamID {};

    check(adapter.getMidiControllerAssignment(0, 0, 1, id)
          == Steinberg::kResultTrue);
    check(id == 0x80000001u);
    check(adapter.getMidiControllerAssignment(0, 15, 127, id)
          == Steinberg::kResultTrue);
    check(id == 0x80000F7Fu);
    check(adapter.getMidiControllerAssignment(0, 0, Vst::kPitchBend, id)
          == Steinberg::kResultTrue);
    check(id == 0x80000081u);
    check(adapter.getMidiControllerAssignment(1, 0, 1, id)
          == Steinberg::kResultFalse);

    // The default is MakeASound's base, for TamberPlug as for Native.
    auto plug = AdapterHost<SynthPlugin> {
        MakeASound::policyFor(MakeASound::IdScheme::TamberPlug, {}).parameters,
        withMidiControllers};
    check(plug.adapter->getMidiControllerAssignment(0, 0, 1, id)
          == Steinberg::kResultTrue);
    check(id == MakeASound::VST3::shadowIdFor(0, 0, 1));
};

auto tPlugGoldens = test("VST3/aTamberPlugPortPublishesWhatPlugsVST3Did") = []
{
    checkPlugsVST3<TamberPlugFixtures::SVFPort>(TamberPlugFixtures::svfPublished);
    checkPlugsVST3<TamberPlugFixtures::ChorusPort>(
        TamberPlugFixtures::chorusPublished);
};
} // namespace
} // namespace VST3Tests
