#include "VST3Common.h"
#include "Adapter.h"
#include "Conversion.h"
#include "HostParameters.h"
#include "PlugViewFactory.h"
#include "Stream.h"
#include "Text.h"
#include "../Compat/JuceState.h"
#include "../Realtime/MessageThread.h"

#include "pluginterfaces/gui/iplugview.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace MakeASound::VST3
{

namespace
{
// What a JUCE VST3 writes, as positively as it can be told: a VST2 bank, the
// private-data trailer its wrapper always appends, or a copyXmlToBinary blob
// from one old enough to have had no trailer. A MakeASound document is JSON, and
// a plugin's own non-JSON document carries none of these.
bool isJuceState(std::string_view bytes)
{
    auto first = bytes.find_first_not_of(" \t\r\n");

    if (first == std::string_view::npos || bytes[first] == '{')
        return false;

    return Compat::readVst2Bank(bytes).has_value()
           || Compat::stripJucePrivateData(bytes).pluginState.size() != bytes.size()
           || Compat::isJuceXmlBlob(bytes);
}
} // namespace

Adapter::Adapter(OwningPointer<Plugin> plugin,
                 ParameterIdentity identity,
                 bool midiControllers)
    : pluginWrapper(std::move(plugin), PluginFormat::VST3, identity)
    , controllers(identity.vst3Controllers)
    , numShadowBuses(
          midiControllers
              ? std::min(pluginWrapper.busLayout().midiInputs.size(),
                         controllers.layout == Vst3ControllerLayout::JuceContiguous
                             ? 1
                             : maxShadowBuses)
              : 0)
{
    this->plugin().setHostEditListener(this);

    const auto& list = this->plugin().parameters();
    lastPluginValue = std::make_unique<std::atomic<float>[]>(
        static_cast<size_t>(std::max(0, list.size())));

    for (auto i = 0; i < list.size(); ++i)
        if (isRegistered(i))
            exposed.add(i);

    takeAllAsBaseline();

    processContextRequirements.needTransportState()
        .needTempo()
        .needTimeSignature()
        .needProjectTimeMusic()
        .needBarPositionMusic()
        .needCycleMusic();

#ifndef NDEBUG
    // The parameter wins and the shadow is not registered. A JUCE port's ids are
    // a JUCE build's, which it cannot move without losing automation, so there
    // the collision is only reported.
    for (const auto& entry: this->plugin().parameters())
    {
        if (!decodeShadowId(controllers, entry.hostId, numShadowBuses))
            continue;

        std::fprintf(stderr,
                     "MakeASound: parameter \"%s\" has host id %u, which a MIDI "
                     "controller's shadow parameter uses; give one an explicit "
                     "hostId\n",
                     entry.id.c_str(),
                     static_cast<unsigned>(entry.hostId));
        assert(list.identity().paramIds == ParamIdRule::JuceKeyHash
               && "a parameter's host id collides with a shadow parameter");
    }
#endif
}

bool Adapter::isRegistered(int index) const noexcept
{
    return pluginWrapper.plugin().parameters().isRegistered(index);
}

Adapter::~Adapter()
{
    plugin().setHostEditListener(nullptr);
}

Plugin& Adapter::plugin() noexcept
{
    return pluginWrapper.plugin();
}

PluginWrapper& Adapter::wrapper() noexcept
{
    return pluginWrapper;
}

tresult PLUGIN_API Adapter::initialize(FUnknown* context)
{
    auto result = SingleComponentEffect::initialize(context);

    if (result != Steinberg::kResultOk)
        return result;

    adoptHostMessageThread();
    addBuses();
    registerParameters();
    registerShadowParameters();

    return Steinberg::kResultOk;
}

void Adapter::addBuses()
{
    const auto& layout = pluginWrapper.busLayout();
    Vst::String128 name {};

    auto busType = [](const Bus& bus)
    { return bus.isMain ? Vst::kMain : Vst::kAux; };
    auto busFlags = [](const Bus& bus)
    { return bus.isMain ? Vst::BusInfo::kDefaultActive : 0; };

    for (const auto& bus: layout.inputs)
    {
        copyTo(name, bus.name);
        addAudioInput(
            name, arrangementFor(bus.numChannels), busType(bus), busFlags(bus));
    }

    for (const auto& bus: layout.outputs)
    {
        copyTo(name, bus.name);
        addAudioOutput(
            name, arrangementFor(bus.numChannels), busType(bus), busFlags(bus));
    }

    for (const auto& bus: layout.midiInputs)
    {
        copyTo(name, bus.name);
        addEventInput(name, 16);
    }

    for (const auto& bus: layout.midiOutputs)
    {
        copyTo(name, bus.name);
        addEventOutput(name, 16);
    }
}

void Adapter::registerParameters()
{
    const auto& list = plugin().parameters();

    for (auto i = 0; i < list.size(); ++i)
        if (isRegistered(i))
            parameters.addParameter(new ProxyParameter(list.entry(i)));
}

void Adapter::registerShadowParameters()
{
    // JUCE's are registered with no flag and a default of 0, pitch bend too.
    auto juce = controllers.layout == Vst3ControllerLayout::JuceContiguous;
    auto flags =
        juce ? 0 : Vst::ParameterInfo::kCanAutomate | Vst::ParameterInfo::kIsHidden;
    auto perChannel = shadowControllersFor(controllers);

    const auto& list = plugin().parameters();
    Vst::String128 title {};

    for (auto bus = 0; bus < numShadowBuses; ++bus)
    {
        for (auto channel = 0; channel < 16; ++channel)
        {
            for (auto controller = 0; controller < perChannel; ++controller)
            {
                auto id = shadowIdFor(controllers, bus, channel, controller);

                if (list.indexOfHostId(id) >= 0)
                    continue;

                auto target = ShadowTarget {bus, channel, controller};
                auto defaultValue =
                    !juce && controller == Vst::kPitchBend ? 0.5 : 0.0;

                copyTo(title, shadowName(controllers, target, numShadowBuses));
                parameters.addParameter(
                    new Vst::Parameter(title, id, nullptr, defaultValue, 0, flags));
            }
        }
    }
}

tresult PLUGIN_API Adapter::terminate()
{
    return SingleComponentEffect::terminate();
}

tresult PLUGIN_API Adapter::setActive(TBool state)
{
    if (state)
    {
        pluginWrapper.prepare(static_cast<int>(std::lround(processSetup.sampleRate)),
                              processSetup.maxSamplesPerBlock);
        pluginWrapper.reset();
        prepared = true;
        active = true;
    }
    else
    {
        active = false;
    }

    return SingleComponentEffect::setActive(state);
}

tresult PLUGIN_API Adapter::setState(IBStream* state)
{
    if (state == nullptr)
        return Steinberg::kInvalidArgument;

    try
    {
        auto document = readAll(*state);
        auto context = stateContextOf(state);

        // A session a JUCE build of the plugin saved goes to the plugin's
        // loadForeignState; off the message thread the wrapper queues it and
        // raises parameterValuesChanged once it ran, which repeats what follows.
        if (isJuceState(document))
            pluginWrapper.loadForeignState(Compat::juceVST3State(document, context));
        else
            pluginWrapper.loadState(document, context);

        // The host re-reads every value, so the next block sends none of them.
        takeAllAsBaseline();
        notifyHostParamValuesChanged();
        notifyHostIfLatencyChanged();
    }
    catch (...)
    {
    }

    return Steinberg::kResultOk;
}

tresult PLUGIN_API Adapter::getState(IBStream* state)
{
    if (state == nullptr)
        return Steinberg::kInvalidArgument;

    try
    {
        auto document = pluginWrapper.saveState(stateContextOf(state));

        if (document.empty())
            return Steinberg::kResultOk;

        return writeAll(*state, document) ? Steinberg::kResultOk
                                          : Steinberg::kResultFalse;
    }
    catch (...)
    {
        return Steinberg::kResultFalse;
    }
}

tresult PLUGIN_API Adapter::setBusArrangements(Vst::SpeakerArrangement* inputs,
                                               int32 numIns,
                                               Vst::SpeakerArrangement* outputs,
                                               int32 numOuts)
{
    const auto& layout = pluginWrapper.busLayout();

    if (active || numIns != layout.inputs.size() || numOuts != layout.outputs.size())
        return Steinberg::kResultFalse;

    auto proposed = layout;

    for (auto i = 0; i < numIns; ++i)
        proposed.inputs[i].numChannels = Vst::SpeakerArr::getChannelCount(inputs[i]);

    for (auto i = 0; i < numOuts; ++i)
        proposed.outputs[i].numChannels =
            Vst::SpeakerArr::getChannelCount(outputs[i]);

    if (!pluginWrapper.setLayout(proposed))
        return Steinberg::kResultFalse;

    return SingleComponentEffect::setBusArrangements(
        inputs, numIns, outputs, numOuts);
}

tresult PLUGIN_API Adapter::canProcessSampleSize(int32 symbolicSampleSize)
{
    return symbolicSampleSize == Vst::kSample32 ? Steinberg::kResultTrue
                                                : Steinberg::kResultFalse;
}

tresult PLUGIN_API Adapter::setProcessing(TBool state)
{
    if (state)
        pluginWrapper.reset();

    return Steinberg::kResultOk;
}

uint32 PLUGIN_API Adapter::getLatencySamples()
{
    auto latency = std::max(0, plugin().latencySamples());
    lastReportedLatency = latency;
    return static_cast<uint32>(latency);
}

uint32 PLUGIN_API Adapter::getTailSamples()
{
    auto tail = plugin().tailSamples();
    return tail <= 0 ? Vst::kNoTail : static_cast<uint32>(tail);
}

void Adapter::notifyHostIfLatencyChanged()
{
    if (!isMessageThread())
    {
        callOnMessageThread([self = Steinberg::IPtr<Adapter>(this)]
                            { self->notifyHostIfLatencyChanged(); });
        return;
    }

    pluginWrapper.takeLatencyChanged();

    if (!componentHandler)
        return;

    auto reported = std::max(0, plugin().latencySamples());
    auto previous = lastReportedLatency.load();

    // Only the caller that moves the baseline restarts the host, so a
    // getLatencySamples() racing in between is never restarted for a stale figure.
    do
    {
        if (previous < 0 || previous == reported)
            return;
    } while (!lastReportedLatency.compare_exchange_weak(previous, reported));

    componentHandler->restartComponent(Vst::kLatencyChanged);
}

void Adapter::notifyHostParamValuesChanged()
{
    if (!isMessageThread())
    {
        callOnMessageThread([self = Steinberg::IPtr<Adapter>(this)]
                            { self->notifyHostParamValuesChanged(); });
        return;
    }

    if (componentHandler)
        componentHandler->restartComponent(Vst::kParamValuesChanged);
}

void Adapter::markHostDirty()
{
    if (!isMessageThread())
    {
        callOnMessageThread([self = Steinberg::IPtr<Adapter>(this)]
                            { self->markHostDirty(); });
        return;
    }

    if (!componentHandler)
        return;

    if (auto handler =
            Steinberg::FUnknownPtr<Vst::IComponentHandler2>(componentHandler))
        handler->setDirty(true);
}

void Adapter::takeAsBaseline(int index) noexcept
{
    lastPluginValue[static_cast<size_t>(index)].store(
        pluginWrapper.getNormalizedParameter(index), std::memory_order_relaxed);
}

void Adapter::takeAllAsBaseline() noexcept
{
    for (auto index: exposed)
        takeAsBaseline(index);
}

tresult PLUGIN_API Adapter::process(Vst::ProcessData& data) noexcept
{
    if (data.symbolicSampleSize != Vst::kSample32)
        return Steinberg::kResultFalse;

    if (!prepared)
    {
        for (auto bus = 0; bus < data.numOutputs; ++bus)
        {
            auto& output = data.outputs[bus];

            if (output.channelBuffers32 == nullptr)
                continue;

            for (auto ch = 0; ch < output.numChannels; ++ch)
                if (auto* channel = output.channelBuffers32[ch])
                    std::fill_n(channel, std::max(0, data.numSamples), 0.f);
        }

        return Steinberg::kResultOk;
    }

    pluginWrapper.clearMidi();

    if (data.inputParameterChanges != nullptr)
        applyParameterChanges(*data.inputParameterChanges);

    pluginWrapper.setPlayhead(data.processContext != nullptr
                                  ? toPlayhead(*data.processContext)
                                  : Playhead {});

    if (data.inputEvents != nullptr)
        collectEvents(*data.inputEvents);

    pluginWrapper.sortMidiInByOffset();

    if (data.numSamples > 0)
    {
        bindBuses(data);
        pluginWrapper.process();

        for (auto bus = 0; bus < data.numOutputs; ++bus)
            data.outputs[bus].silenceFlags = 0;

        if (data.outputEvents != nullptr)
            emitEvents(*data.outputEvents);
    }

    // A zero-sample flush carries them too, so a stopped host still hears them.
    if (data.outputParameterChanges != nullptr)
        emitParameterChanges(*data.outputParameterChanges);

    return Steinberg::kResultOk;
}

// One point at offset 0 per exposed parameter that moved since the host last
// heard of it: a preset the plugin loaded itself, a randomize, a MIDI learn. A
// held parameter is the editor's gesture, which performEdit already reports.
void Adapter::emitParameterChanges(Vst::IParameterChanges& changes) noexcept
{
    const auto& list = plugin().parameters();

    for (auto index: exposed)
    {
        auto value = pluginWrapper.getNormalizedParameter(index);
        auto& last = lastPluginValue[static_cast<size_t>(index)];

        if (value == last.load(std::memory_order_relaxed))
            continue;

        last.store(value, std::memory_order_relaxed);

        if (pluginWrapper.isParameterHeld(index))
            continue;

        auto queueIndex = int32 {};
        auto pointIndex = int32 {};

        if (auto* queue =
                changes.addParameterData(list.entry(index).hostId, queueIndex))
            queue->addPoint(0, value, pointIndex);
    }
}

void Adapter::applyParameterChanges(Vst::IParameterChanges& changes) noexcept
{
    const auto& list = plugin().parameters();
    auto numQueues = changes.getParameterCount();

    for (auto q = 0; q < numQueues; ++q)
    {
        auto* queue = changes.getParameterData(q);

        if (queue == nullptr)
            continue;

        auto id = queue->getParameterId();
        auto numPoints = queue->getPointCount();
        auto offset = int32 {};
        auto value = Vst::ParamValue {};

        if (auto index = list.indexOfHostId(id); index >= 0)
        {
            if (numPoints > 0
                && queue->getPoint(numPoints - 1, offset, value)
                       == Steinberg::kResultOk)
            {
                pluginWrapper.setNormalizedParameter(index,
                                                     static_cast<float>(value));

                // Not echoed back; a held parameter that dropped the write is
                // the gesture's to report.
                takeAsBaseline(index);
            }

            continue;
        }

        auto target = decodeShadowId(controllers, id, numShadowBuses);

        if (!target)
            continue;

        for (auto p = 0; p < numPoints; ++p)
            if (queue->getPoint(p, offset, value) == Steinberg::kResultOk)
                pluginWrapper.pushMidiIn(
                    target->bus,
                    shadowEvent(*target, offset, static_cast<float>(value)));
    }
}

void Adapter::collectEvents(Vst::IEventList& events) noexcept
{
    auto numBuses = pluginWrapper.busLayout().midiInputs.size();
    auto numEvents = events.getEventCount();

    for (auto i = 0; i < numEvents; ++i)
    {
        auto in = Vst::Event {};
        auto out = MIDI::Event {};

        if (events.getEvent(i, in) != Steinberg::kResultOk || !fromVst3(in, out))
            continue;

        // Live sends INT_MIN.
        auto bus = in.busIndex >= 0 && in.busIndex < numBuses ? in.busIndex : 0;
        pluginWrapper.pushMidiIn(bus, out);
    }
}

void Adapter::bindBuses(Vst::ProcessData& data) noexcept
{
    const auto& layout = pluginWrapper.busLayout();

    auto channelsOf = [](const Vst::AudioBusBuffers& bus)
    {
        return bus.channelBuffers32 == nullptr ? 0
                                               : static_cast<int>(bus.numChannels);
    };

    auto numInputs =
        std::min(layout.inputs.size(), static_cast<int>(data.numInputs));

    for (auto bus = 0; bus < numInputs; ++bus)
    {
        const auto& input = data.inputs[bus];
        pluginWrapper.bindInput(
            bus, input.channelBuffers32, channelsOf(input), data.numSamples);
    }

    auto numOutputs =
        std::min(layout.outputs.size(), static_cast<int>(data.numOutputs));

    for (auto bus = 0; bus < numOutputs; ++bus)
    {
        const auto& output = data.outputs[bus];
        const float* const* matching = nullptr;
        auto matchingChannels = 0;

        if (bus < data.numInputs)
        {
            matching = data.inputs[bus].channelBuffers32;
            matchingChannels = channelsOf(data.inputs[bus]);
        }

        pluginWrapper.bindOutput(bus,
                                 output.channelBuffers32,
                                 channelsOf(output),
                                 data.numSamples,
                                 matching,
                                 matchingChannels);
    }
}

void Adapter::emitEvents(Vst::IEventList& events) noexcept
{
    const auto& buses = pluginWrapper.midiOut();

    for (auto bus = 0; bus < buses.size(); ++bus)
    {
        for (const auto& event: buses[bus])
        {
            auto out = Vst::Event {};

            if (toVst3(event, bus, out))
                events.addEvent(out);
        }
    }
}

tresult PLUGIN_API Adapter::setComponentState(IBStream*)
{
    return Steinberg::kResultOk;
}

Vst::ParamValue PLUGIN_API Adapter::getParamNormalized(Vst::ParamID id)
{
    if (auto index = plugin().parameters().indexOfHostId(id); index >= 0)
        return pluginWrapper.getNormalizedParameter(index);

    return SingleComponentEffect::getParamNormalized(id);
}

tresult PLUGIN_API Adapter::setParamNormalized(Vst::ParamID id,
                                               Vst::ParamValue value)
{
    if (auto index = plugin().parameters().indexOfHostId(id); index >= 0)
    {
        pluginWrapper.setNormalizedParameter(index, static_cast<float>(value));
        takeAsBaseline(index);
        notifyHostIfLatencyChanged();
        return Steinberg::kResultTrue;
    }

    return SingleComponentEffect::setParamNormalized(id, value);
}

Steinberg::IPlugView* PLUGIN_API Adapter::createView(FIDString name)
{
    if (name == nullptr || std::string_view(name) != Vst::ViewType::kEditor)
        return nullptr;

    return createPlugView(*static_cast<Vst::IAudioProcessor*>(this), plugin());
}

tresult PLUGIN_API Adapter::getMidiControllerAssignment(int32 busIndex,
                                                        Steinberg::int16 channel,
                                                        Vst::CtrlNumber controller,
                                                        Vst::ParamID& id)
{
    // JUCE's layout answers for any bus, as JUCE does, with bus 0's ids.
    auto juce = controllers.layout == Vst3ControllerLayout::JuceContiguous;

    if (busIndex < 0 || (!juce && busIndex >= numShadowBuses) || numShadowBuses == 0
        || channel < 0 || channel > 15 || controller < 0
        || controller >= shadowControllersFor(controllers))
        return Steinberg::kResultFalse;

    auto shadow = shadowIdFor(controllers, juce ? 0 : busIndex, channel, controller);

    if (plugin().parameters().indexOfHostId(shadow) >= 0)
        return Steinberg::kResultFalse;

    id = shadow;
    return Steinberg::kResultTrue;
}

tresult PLUGIN_API Adapter::setChannelContextInfos(Vst::IAttributeList* list)
{
    if (list == nullptr)
        return Steinberg::kInvalidArgument;

    try
    {
        pluginWrapper.setTrackInfo(toTrackInfo(*list));
    }
    catch (...)
    {
        return Steinberg::kResultFalse;
    }

    return Steinberg::kResultOk;
}

void Adapter::beginParameterEdit(int index) noexcept
{
    pluginWrapper.holdParameter(index);

    if (isRegistered(index))
        beginEdit(plugin().parameters().entry(index).hostId);
}

void Adapter::performParameterEdit(int index, float normalized) noexcept
{
    if (isRegistered(index))
    {
        performEdit(plugin().parameters().entry(index).hostId, normalized);
        takeAsBaseline(index);
    }

    notifyHostIfLatencyChanged();
}

void Adapter::endParameterEdit(int index) noexcept
{
    if (isRegistered(index))
        endEdit(plugin().parameters().entry(index).hostId);

    pluginWrapper.releaseParameter(index);
}

void Adapter::beginParameterEditGroup() noexcept
{
    startGroupEdit();
}

void Adapter::endParameterEditGroup() noexcept
{
    finishGroupEdit();
}

void Adapter::latencyChanged() noexcept
{
    notifyHostIfLatencyChanged();
}

void Adapter::parameterInfoChanged() noexcept
{
    const auto& list = plugin().parameters();

    for (auto i = 0; i < list.size(); ++i)
        if (isRegistered(i))
            if (auto* proxy = parameters.getParameter(list.entry(i).hostId))
                static_cast<ProxyParameter*>(proxy)->refreshInfo();

    if (componentHandler)
        componentHandler->restartComponent(Vst::kParamTitlesChanged);
}

void Adapter::parameterValuesChanged() noexcept
{
    takeAllAsBaseline();
    notifyHostParamValuesChanged();
    notifyHostIfLatencyChanged();
}

void Adapter::stateChanged() noexcept
{
    markHostDirty();
}

tresult PLUGIN_API Adapter::queryInterface(const Steinberg::TUID _iid, void** obj)
{
    DEF_INTERFACE(Vst::IMidiMapping)
    DEF_INTERFACE(Vst::ChannelContext::IInfoListener)
    return SingleComponentEffect::queryInterface(_iid, obj);
}

} // namespace MakeASound::VST3
