#include "../../Plugin/VST3/VST3Common.h"
#include "VST3HostedPlugin.h"
#include "../JuceHostCompat.h"

#include <base/source/fobject.h>
#include <public.sdk/source/common/memorystream.h>
#include <public.sdk/source/vst/utility/stringconvert.h>
#include <pluginterfaces/vst/ivstunits.h>
#include <pluginterfaces/vst/vstspeaker.h>

#include <algorithm>
#include <cmath>
#include <map>

namespace MakeASound::Hosting
{

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace
{
// Room in the parameter-change queues past the plugin's own count: a plugin may
// report ids it never listed (its MIDI-mapped controllers), and a queue that
// grows in process() allocates.
constexpr int changeQueueHeadroom = 256;

// One bit per channel: every bit set says the whole bus is silent.
constexpr auto allChannelsSilent = ~uint64 {0};

SpeakerArrangement arrangementFor(int numChannels) noexcept
{
    using namespace SpeakerArr;

    switch (numChannels)
    {
        case 0:
            return kEmpty;
        case 1:
            return kMono;
        case 2:
            return kStereo;
        case 3:
            return k30Music;
        case 4:
            return k40Music;
        case 5:
            return k50;
        case 6:
            return k51;
        case 7:
            return k61Music;
        case 8:
            return k71Music;
        default:
            return kEmpty;
    }
}

// The SDK's, not MakeASound's of the same names.
using VstChanges = Steinberg::Vst::ParameterChanges;
using VstContext = Steinberg::Vst::ProcessContext;

void addPoint(VstChanges& changes, ParamID id, double value, int offset)
{
    auto queueIndex = int32 {};
    auto pointIndex = int32 {};

    if (auto* queue = changes.addParameterData(id, queueIndex))
        queue->addPoint(offset, value, pointIndex);
}

float from7Bit(int value) noexcept
{
    return static_cast<float>(std::clamp(value, 0, 127)) / 127.f;
}

// A controller, channel pressure, bend or program change the plugin sent out:
// VST3 carries those as LegacyMIDICCOutEvent, in 7-bit values.
void addLegacyController(const LegacyMIDICCOutEvent& cc,
                         int offset,
                         MIDI::Buffer& target) noexcept
{
    auto channel = static_cast<int>(cc.channel);

    if (cc.controlNumber < 128)
    {
        target.add(MIDI::Event::controlChange(
            channel, cc.controlNumber, from7Bit(cc.value), offset));
        return;
    }

    switch (cc.controlNumber)
    {
        case kAfterTouch:
            target.add(
                MIDI::Event::channelAftertouch(channel, from7Bit(cc.value), offset));
            break;

        case kPitchBend:
        {
            constexpr auto centre = 8192.f;
            auto raw = (cc.value & 0x7f) | ((cc.value2 & 0x7f) << 7);
            target.add(MIDI::Event::pitchBend(
                channel, (static_cast<float>(raw) - centre) / centre, offset));
            break;
        }

        case kCtrlProgramChange:
            target.add(MIDI::Event::programChange(
                channel, std::clamp(static_cast<int>(cc.value), 0, 127), offset));
            break;

        default:
            break;
    }
}

int numSamplesOf(const ProcessContext& context) noexcept
{
    if (!context.outputs.empty())
        return context.outputs[0].getNumSamples();

    if (!context.inputs.empty())
        return context.inputs[0].getNumSamples();

    return 0;
}
} // namespace

// The host side of every edit the plugin makes in its own GUI. In VST3 the HOST
// tells the processor (inputParameterChanges), so without this a knob turned in
// the plugin's window changes the controller and nothing else.
struct VST3HostedPlugin::ComponentHandler
    : public FObject
    , public IComponentHandler
{
    explicit ComponentHandler(VST3HostedPlugin& ownerToUse)
        : owner(&ownerToUse)
    {
    }

    tresult PLUGIN_API beginEdit(ParamID id) override
    {
        if (owner == nullptr)
            return kResultFalse;

        owner->beginEdit(id);
        return kResultOk;
    }

    tresult PLUGIN_API performEdit(ParamID id, ParamValue normalized) override
    {
        if (owner == nullptr)
            return kResultFalse;

        owner->performEdit(id, normalized);
        return kResultOk;
    }

    tresult PLUGIN_API endEdit(ParamID id) override
    {
        if (owner == nullptr)
            return kResultFalse;

        owner->endEdit(id);
        return kResultOk;
    }

    tresult PLUGIN_API restartComponent(int32 flags) override
    {
        if (owner == nullptr)
            return kResultFalse;

        owner->restart(flags);
        return kResultOk;
    }

    // The plugin holds a reference, and may release it after the owner is gone.
    void detach() noexcept { owner = nullptr; }

    // clang-format off
    OBJ_METHODS(ComponentHandler, FObject)
    REFCOUNT_METHODS(FObject)
    DEFINE_INTERFACES
        DEF_INTERFACE(IComponentHandler)
    END_DEFINE_INTERFACES(FObject)
    // clang-format on

    VST3HostedPlugin* owner = nullptr;
};

VST3HostedPlugin::VST3HostedPlugin(
    PluginDescription description,
    std::shared_ptr<::VST3::Hosting::Module> moduleToUse,
    IPtr<IHostApplication> hostToUse,
    IPtr<IComponent> componentToUse,
    IPtr<IEditController> controllerToUse,
    bool separateControllerToUse)
    : HostedPlugin(std::move(description))
    , module(std::move(moduleToUse))
    , hostApplication(std::move(hostToUse))
    , component(std::move(componentToUse))
    , controller(std::move(controllerToUse))
    , separateController(separateControllerToUse)
{
    processor = FUnknownPtr<IAudioProcessor>(component);

    if (controller)
        midiMapping = FUnknownPtr<IMidiMapping>(controller);

    readBusLayout();
    buildParameters();

    // Installed last: the plugin may call straight back into performEdit or
    // restartComponent from here, and everything those reach must exist.
    if (controller)
    {
        handler = owned(new ComponentHandler(*this));
        controller->setComponentHandler(handler);
    }
}

VST3HostedPlugin::~VST3HostedPlugin()
{
    stopProcessing();

    // Before terminate(), and before anything the handler reaches is destroyed.
    if (controller)
        controller->setComponentHandler(nullptr);

    if (handler)
        handler->detach();

    if (separateController)
    {
        auto componentPoint = FUnknownPtr<IConnectionPoint>(component);
        auto controllerPoint = FUnknownPtr<IConnectionPoint>(controller);

        if (componentPoint && controllerPoint)
        {
            componentPoint->disconnect(controllerPoint);
            controllerPoint->disconnect(componentPoint);
        }

        controller->terminate();
    }

    controller = nullptr;
    midiMapping = nullptr;
    processor = nullptr;

    if (component)
        component->terminate();
}

void VST3HostedPlugin::readBusLayout()
{
    layout = BusLayout();

    auto collectAudio = [this](BusDirection direction, Vector<Bus>& target)
    {
        for (auto i = int32 {0}; i < component->getBusCount(kAudio, direction); ++i)
        {
            auto info = BusInfo();

            if (component->getBusInfo(kAudio, direction, i, info) != kResultOk)
                continue;

            auto bus = Bus();
            bus.name = StringConvert::convert(info.name);
            bus.numChannels = info.channelCount;
            bus.isMain = info.busType == kMain;
            bus.activeByDefault = (info.flags & BusInfo::kDefaultActive) != 0;
            target.add(bus);
        }
    };

    auto collectMidi = [this](BusDirection direction, Vector<MidiBus>& target)
    {
        for (auto i = int32 {0}; i < component->getBusCount(kEvent, direction); ++i)
        {
            auto info = BusInfo();

            if (component->getBusInfo(kEvent, direction, i, info) != kResultOk)
                continue;

            auto bus = MidiBus();
            bus.name = StringConvert::convert(info.name);
            bus.activeByDefault = (info.flags & BusInfo::kDefaultActive) != 0;
            target.add(bus);
        }
    };

    collectAudio(kInput, layout.inputs);
    collectAudio(kOutput, layout.outputs);
    collectMidi(kInput, layout.midiInputs);
    collectMidi(kOutput, layout.midiOutputs);
}

namespace
{
// Each unit's parent, as JUCE's host reads them (initialiseParameterList): the
// IUnitInfo of the component, else of the controller, from unit index 1, the
// root (index 0) being its own.
std::map<UnitID, UnitID> unitParents(IComponent* component,
                                     IEditController* controller)
{
    auto parents = std::map<UnitID, UnitID>();
    auto units = FUnknownPtr<IUnitInfo>(component);

    if (!units)
        units = FUnknownPtr<IUnitInfo>(controller);

    if (!units)
        return parents;

    for (auto i = int32 {1}; i < units->getUnitCount(); ++i)
    {
        auto unit = UnitInfo();
        units->getUnitInfo(i, unit);
        parents[unit.id] = unit.parentUnitId;
    }

    return parents;
}

// A unit's group path, outermost first. An unknown unit (or parent) is the
// root, as JUCE files it.
Vector<int64_t> unitPath(UnitID unit, const std::map<UnitID, UnitID>& parents)
{
    constexpr auto maxDepth = 64;
    auto path = Vector<int64_t>();

    while (unit != kRootUnitId && path.size() < maxDepth)
    {
        auto found = parents.find(unit);

        if (found == parents.end())
            break;

        path.insert(0, static_cast<int64_t>(unit));
        unit = found->second;
    }

    return path;
}
} // namespace

void VST3HostedPlugin::buildParameters()
{
    // Per controller parameter: its storage index or -1, and its unit path.
    auto listed = Vector<int>();
    auto paths = Vector<Vector<int64_t>>();

    if (controller)
    {
        auto parents = unitParents(component, controller);

        for (auto i = int32 {0}; i < controller->getParameterCount(); ++i)
        {
            auto info = ParameterInfo();
            auto valid = controller->getParameterInfo(i, info) == kResultOk;

            paths.add(valid ? unitPath(info.unitId, parents) : Vector<int64_t>());

            // A hidden parameter is the plugin's own plumbing, most often the
            // thousands its IMidiMapping maps controllers onto: never shown, and
            // reached through MIDI rather than by index.
            if (valid && (info.flags & ParameterInfo::kIsHidden) == 0)
            {
                listed.add(storage.size());
                storage.add(EA::makeOwned<VST3HostedParameter>(
                    *controller, info, static_cast<VST3ParameterSink&>(*this)));
            }
            else
            {
                listed.add(-1);
            }
        }
    }

    // JUCE counted every parameter, in its unit tree's order.
    for (auto position: Compat::juceParameterTreeOrder(paths))
        controllerIndices.add(listed[position]);

    group.addAll(storage);

    for (auto& param: storage)
        byId.add(param.get());

    std::sort(byId.begin(),
              byId.end(),
              [](const VST3HostedParameter* a, const VST3HostedParameter* b)
              { return a->paramId() < b->paramId(); });

    setParameters(ParameterList {group, hostedPluginIdentity()});
}

VST3HostedParameter* VST3HostedPlugin::findParameter(ParamID id) const noexcept
{
    auto before = [](const VST3HostedParameter* param, ParamID target)
    { return param->paramId() < target; };
    auto found = std::lower_bound(byId.begin(), byId.end(), id, before);

    if (found == byId.end() || (*found)->paramId() != id)
        return nullptr;

    return *found;
}

int VST3HostedPlugin::indexOfParamId(ParamID id) const noexcept
{
    return parameters().indexOf(findParameter(id));
}

int VST3HostedPlugin::indexForControllerIndex(int32_t controllerIndex) const noexcept
{
    if (controllerIndex < 0 || controllerIndex >= controllerIndices.size())
        return -1;

    return controllerIndices[static_cast<int>(controllerIndex)];
}

bool VST3HostedPlugin::applyArrangement(const BusLayout& proposed) const
{
    if (!processor)
        return false;

    auto side = [](const Vector<Bus>& buses)
    {
        auto arrangements = std::vector<SpeakerArrangement>();

        for (const auto& bus: buses)
            arrangements.push_back(arrangementFor(bus.numChannels));

        return arrangements;
    };

    auto inputs = side(proposed.inputs);
    auto outputs = side(proposed.outputs);

    return processor->setBusArrangements(inputs.empty() ? nullptr : inputs.data(),
                                         static_cast<int32>(inputs.size()),
                                         outputs.empty() ? nullptr : outputs.data(),
                                         static_cast<int32>(outputs.size()))
           == kResultTrue;
}

bool VST3HostedPlugin::acceptsLayout(const BusLayout& proposed) const
{
    // The plugin's buses are its own: only their channel counts can be asked
    // for, and the plugin answers.
    if (proposed.inputs.size() != layout.inputs.size()
        || proposed.outputs.size() != layout.outputs.size())
        return false;

    auto accepted = applyArrangement(proposed);

    // Asking changes the plugin's arrangement; the one it was prepared with
    // stands until the next prepare().
    applyArrangement(layout);
    return accepted;
}

void VST3HostedPlugin::stopProcessing()
{
    prepared.store(false, std::memory_order_release);

    if (processor && processing)
        processor->setProcessing(false);

    processing = false;

    if (component && active)
        component->setActive(false);

    active = false;
}

void VST3HostedPlugin::activateBuses(bool shouldBeActive)
{
    // Only the buses this host drives: a main bus always, an aux bus as the
    // plugin declared it. Switching on an aux bus the plugin ships off (a synth's
    // sidechain) makes it one the plugin may read every block.
    auto activate = [&](MediaType media, BusDirection direction)
    {
        for (auto i = int32 {0}; i < component->getBusCount(media, direction); ++i)
        {
            auto info = BusInfo();

            if (component->getBusInfo(media, direction, i, info) != kResultOk)
                continue;

            auto wanted =
                info.busType == kMain || (info.flags & BusInfo::kDefaultActive) != 0;
            component->activateBus(media, direction, i, shouldBeActive && wanted);
        }
    };

    activate(kAudio, kInput);
    activate(kAudio, kOutput);
    activate(kEvent, kInput);
    activate(kEvent, kOutput);
}

void VST3HostedPlugin::prepare(const ProcessSpec& spec)
{
    if (!processor || !component)
        return;

    // setupProcessing() must run while the component is inactive.
    stopProcessing();

    if (spec.layout.inputs.size() == layout.inputs.size()
        && spec.layout.outputs.size() == layout.outputs.size()
        && applyArrangement(spec.layout))
    {
        for (auto i = 0; i < layout.inputs.size(); ++i)
            layout.inputs[i].numChannels = spec.layout.inputs[i].numChannels;

        for (auto i = 0; i < layout.outputs.size(); ++i)
            layout.outputs[i].numChannels = spec.layout.outputs[i].numChannels;
    }
    else
    {
        applyArrangement(layout);
    }

    sampleRate = spec.sampleRate;
    maxBlockSize = std::max(spec.maxBlockSize, 0);
    rewindFallbackPlayhead();

    // Everything process() touches, sized here: a block must not allocate.
    inputBuses.resize(layout.inputs.size());
    outputBuses.resize(layout.outputs.size());
    inputTables.resize(layout.inputs.size());
    outputTables.resize(layout.outputs.size());

    auto channels = [](const Bus& bus)
    { return static_cast<size_t>(std::max(0, bus.numChannels)); };

    for (auto i = 0; i < layout.inputs.size(); ++i)
        inputTables[i].assign(channels(layout.inputs[i]), nullptr);

    for (auto i = 0; i < layout.outputs.size(); ++i)
        outputTables[i].assign(channels(layout.outputs[i]), nullptr);

    silence.assign(static_cast<size_t>(maxBlockSize), 0.f);
    scratch.assign(static_cast<size_t>(maxBlockSize), 0.f);

    auto maxParameters = storage.size() + changeQueueHeadroom;
    inputChanges.setMaxParameters(maxParameters);
    outputChanges.setMaxParameters(maxParameters);
    inputEvents.setMaxSize(ProcessContext::defaultMidiCapacity);
    outputEvents.setMaxSize(ProcessContext::defaultMidiCapacity);

    auto setup = ProcessSetup();
    setup.processMode = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = maxBlockSize;
    setup.sampleRate = sampleRate;
    processor->setupProcessing(setup);

    activateBuses(true);

    component->setActive(true);
    active = true;
    processor->setProcessing(true);
    processing = true;

    latency.store(static_cast<int>(processor->getLatencySamples()));
    prepared.store(true, std::memory_order_release);
}

int VST3HostedPlugin::latencySamples() const noexcept
{
    return latency.load();
}

int VST3HostedPlugin::tailSamples() const noexcept
{
    if (!processor)
        return 0;

    auto tail = processor->getTailSamples();

    if (tail == kInfiniteTail)
        return std::numeric_limits<int>::max();

    return static_cast<int>(std::min<uint32>(tail, std::numeric_limits<int>::max()));
}

void VST3HostedPlugin::bindAudio(ProcessContext& context, int numSamples) noexcept
{
    for (auto i = 0; i < inputBuses.size(); ++i)
    {
        auto* supplied = i < context.inputs.size() && !context.inputs[i].isEmpty()
                             ? &context.inputs[i]
                             : nullptr;
        auto& table = inputTables[i];

        for (auto ch = size_t {0}; ch < table.size(); ++ch)
        {
            auto channel = static_cast<int>(ch);
            table[ch] = supplied != nullptr && channel < supplied->getNumChannels()
                            ? supplied->getChannelPointer(channel)
                            : silence.data();
        }

        inputBuses[i].numChannels = static_cast<int32>(table.size());
        inputBuses[i].silenceFlags = supplied != nullptr ? 0 : allChannelsSilent;
        inputBuses[i].channelBuffers32 = table.data();
    }

    for (auto i = 0; i < outputBuses.size(); ++i)
    {
        auto* supplied = i < context.outputs.size() && !context.outputs[i].isEmpty()
                             ? &context.outputs[i]
                             : nullptr;
        auto& table = outputTables[i];

        for (auto ch = size_t {0}; ch < table.size(); ++ch)
        {
            auto channel = static_cast<int>(ch);
            table[ch] = supplied != nullptr && channel < supplied->getNumChannels()
                            ? supplied->getChannelPointer(channel)
                            : scratch.data();
        }

        outputBuses[i].numChannels = static_cast<int32>(table.size());
        outputBuses[i].silenceFlags = 0;
        outputBuses[i].channelBuffers32 = table.data();
    }

    // A silent input is still read by the plugin: it must hold zeros.
    std::fill(silence.begin(), silence.begin() + numSamples, 0.f);
}

void VST3HostedPlugin::process(ProcessContext& context) noexcept
{
    if (!prepared.load(std::memory_order_acquire))
        return;

    auto numSamples = numSamplesOf(context);

    // setupProcessing() promised no longer block, and the stand-ins are sized to it.
    if (numSamples <= 0 || numSamples > maxBlockSize)
        return;

    bindAudio(context, numSamples);

    auto data = ProcessData();
    data.processMode = kRealtime;
    data.symbolicSampleSize = kSample32;
    data.numSamples = numSamples;
    data.numInputs = static_cast<int32>(inputBuses.size());
    data.numOutputs = static_cast<int32>(outputBuses.size());
    data.inputs = inputBuses.empty() ? nullptr : inputBuses.data();
    data.outputs = outputBuses.empty() ? nullptr : outputBuses.data();
    data.inputParameterChanges = &inputChanges;
    data.outputParameterChanges = &outputChanges;
    data.inputEvents = &inputEvents;
    data.outputEvents = &outputEvents;

    fillProcessContext(playheadFor(context.playhead, numSamples, sampleRate));
    data.processContext = &vstContext;

    // Before the MIDI, so a controller this block lands after a pending edit of
    // the same parameter.
    drainEdits();
    pushMidi(context);

    processor->process(data);

    readOutputParameters();
    readOutputEvents(context);

    inputChanges.clearQueue();
    outputChanges.clearQueue();
    inputEvents.clear();
    outputEvents.clear();
}

bool VST3HostedPlugin::setParameterFromAudioThread(int index,
                                                   float normalized) noexcept
{
    if (index < 0 || index >= storage.size() || !std::isfinite(normalized)
        || !prepared.load(std::memory_order_acquire))
        return false;

    auto& param = *storage[index];
    auto value = std::clamp(normalized, 0.f, 1.f);

    // At offset 0 a repeat overwrites the point rather than adding one, so the
    // queue's reserved points are enough.
    addPoint(inputChanges, param.paramId(), value, 0);

    param.store(value);
    param.markChangedByProcessor();
    outputDirty.store(true, std::memory_order_release);
    return true;
}

void VST3HostedPlugin::drainEdits() noexcept
{
    auto edit = ParamEdit();

    while (pendingEdits.pop(edit))
        addPoint(inputChanges, edit.id, edit.normalized, 0);
}

void VST3HostedPlugin::pushMidi(ProcessContext& context) noexcept
{
    auto mapped = [this](int bus, int channel, CtrlNumber number, ParamID& id)
    {
        return midiMapping
               && midiMapping->getMidiControllerAssignment(
                      bus, static_cast<int16>(channel), number, id)
                      == kResultOk;
    };

    auto buses = std::min(context.midiIn.size(), layout.midiInputs.size());

    for (auto bus = 0; bus < buses; ++bus)
    {
        for (const auto& event: context.midiIn[bus])
        {
            auto channel = static_cast<int16>(event.channel & 0x0f);
            auto out = Event();
            out.busIndex = bus;
            out.sampleOffset = event.sampleOffset;

            if (const auto* on = event.asNoteOn())
            {
                out.type = Event::kNoteOnEvent;
                out.noteOn.channel = channel;
                out.noteOn.pitch = static_cast<int16>(on->pitch);
                out.noteOn.velocity = on->velocity;
                out.noteOn.noteId = -1;
                inputEvents.addEvent(out);
            }
            else if (const auto* off = event.asNoteOff())
            {
                out.type = Event::kNoteOffEvent;
                out.noteOff.channel = channel;
                out.noteOff.pitch = static_cast<int16>(off->pitch);
                out.noteOff.velocity = off->velocity;
                out.noteOff.noteId = -1;
                inputEvents.addEvent(out);
            }
            else if (const auto* poly = event.asPolyAftertouch())
            {
                out.type = Event::kPolyPressureEvent;
                out.polyPressure.channel = channel;
                out.polyPressure.pitch = static_cast<int16>(poly->pitch);
                out.polyPressure.pressure = poly->pressure;
                out.polyPressure.noteId = -1;
                inputEvents.addEvent(out);
            }
            else if (const auto* sysEx = event.asSysEx())
            {
                // F0 to F7, as JUCE's host sends it. The bytes are the input
                // event's own, which outlives the block, so nothing is copied.
                if (sysEx->size <= 0)
                    continue;

                out.type = Event::kDataEvent;
                out.data.type = DataEvent::kMidiSysEx;
                out.data.size = static_cast<uint32>(sysEx->size);
                out.data.bytes = sysEx->data.data();
                inputEvents.addEvent(out);
            }
            else
            {
                // Controllers, bend, channel pressure and program change reach a
                // VST3 processor as parameters, through IMidiMapping.
                auto id = ParamID {};
                auto value = 0.0;
                auto found = false;

                if (const auto* cc = event.asControlChange())
                {
                    found = mapped(bus, channel, cc->controller, id);
                    value = cc->value;
                }
                else if (const auto* bend = event.asPitchBend())
                {
                    found = mapped(bus, channel, kPitchBend, id);
                    value = std::clamp((bend->value + 1.f) * 0.5f, 0.f, 1.f);
                }
                else if (const auto* touch = event.asChannelAftertouch())
                {
                    found = mapped(bus, channel, kAfterTouch, id);
                    value = touch->pressure;
                }
                else if (const auto* program = event.asProgramChange())
                {
                    found = mapped(bus, channel, kCtrlProgramChange, id);
                    value = program->program / 127.0;
                }

                if (found)
                    addPoint(inputChanges, id, value, event.sampleOffset);
            }
        }
    }
}

void VST3HostedPlugin::readOutputParameters() noexcept
{
    for (auto i = int32 {0}; i < outputChanges.getParameterCount(); ++i)
    {
        auto* queue = outputChanges.getParameterData(i);

        if (queue == nullptr || queue->getPointCount() <= 0)
            continue;

        // The block's last value is all a cache needs.
        auto offset = int32 {};
        auto value = ParamValue {};
        auto last = queue->getPointCount() - 1;

        if (queue->getPoint(last, offset, value) != kResultTrue)
            continue;

        if (auto* param = findParameter(queue->getParameterId()))
        {
            // The controller is the message thread's: syncEditorWithProcessor().
            param->store(static_cast<float>(value));
            param->markChangedByProcessor();
            outputDirty.store(true, std::memory_order_release);
        }
    }
}

void VST3HostedPlugin::readOutputEvents(ProcessContext& context) noexcept
{
    for (auto i = int32 {0}; i < outputEvents.getEventCount(); ++i)
    {
        auto event = Event();

        if (outputEvents.getEvent(i, event) != kResultOk || event.busIndex < 0
            || event.busIndex >= context.midiOut.size())
            continue;

        auto& target = context.midiOut[event.busIndex];

        // Reserved in prepare(); a full buffer drops rather than grows.
        if (target.size() >= target.capacity())
            continue;

        switch (event.type)
        {
            case Event::kNoteOnEvent:
                target.add(MIDI::Event::noteOn(event.noteOn.channel,
                                               event.noteOn.pitch,
                                               event.noteOn.velocity,
                                               event.sampleOffset));
                break;

            case Event::kNoteOffEvent:
                target.add(MIDI::Event::noteOff(event.noteOff.channel,
                                                event.noteOff.pitch,
                                                event.noteOff.velocity,
                                                event.sampleOffset));
                break;

            case Event::kPolyPressureEvent:
                target.add(MIDI::Event::polyAftertouch(event.polyPressure.channel,
                                                       event.polyPressure.pitch,
                                                       event.polyPressure.pressure,
                                                       event.sampleOffset));
                break;

            case Event::kLegacyMIDICCOutEvent:
                addLegacyController(event.midiCCOut, event.sampleOffset, target);
                break;

            case Event::kDataEvent:
            {
                // Copied into the event, so the plugin's bytes may go with its
                // list. Past MIDI::SysEx::maxBytes it is dropped, as the Audio
                // Unit host drops it.
                auto size = static_cast<int>(event.data.size);

                if (event.data.type == DataEvent::kMidiSysEx
                    && event.data.bytes != nullptr && size > 0
                    && size <= MIDI::SysEx::maxBytes)
                    target.add(MIDI::Event::sysEx(
                        event.data.bytes, size, event.sampleOffset));
                break;
            }

            default:
                break;
        }
    }
}

void VST3HostedPlugin::fillProcessContext(const Playhead& playhead) noexcept
{
    vstContext = VstContext();
    vstContext.sampleRate = sampleRate;
    vstContext.projectTimeSamples = playhead.sampleTime;
    vstContext.continousTimeSamples = playhead.sampleTime;
    vstContext.projectTimeMusic = playhead.ppqPosition;
    vstContext.barPositionMusic = playhead.barStartPpq;
    vstContext.cycleStartMusic = playhead.loopStartPpq;
    vstContext.cycleEndMusic = playhead.loopEndPpq;
    vstContext.tempo = playhead.bpm;
    vstContext.timeSigNumerator = playhead.timeSignature.numerator;
    vstContext.timeSigDenominator = playhead.timeSignature.denominator;

    auto state =
        uint32 {VstContext::kTempoValid | VstContext::kTimeSigValid
                | VstContext::kContTimeValid | VstContext::kProjectTimeMusicValid
                | VstContext::kBarPositionValid};

    if (playhead.isPlaying)
        state |= VstContext::kPlaying;

    if (playhead.isRecording)
        state |= VstContext::kRecording;

    if (playhead.isLooping)
        state |= VstContext::kCycleActive | VstContext::kCycleValid;

    vstContext.state = state;
}

void VST3HostedPlugin::syncEditorWithProcessor()
{
    if (!controller || !outputDirty.exchange(false, std::memory_order_acq_rel))
        return;

    for (auto& param: storage)
        if (param->takeChangedByProcessor())
            controller->setParamNormalized(param->paramId(), param->getValue());
}

void VST3HostedPlugin::parameterEdited(ParamID id, float normalized)
{
    // Both halves of a host-side edit: the controller, for the plugin's GUI, and
    // the processor's next block.
    if (controller)
        controller->setParamNormalized(id, normalized);

    pendingEdits.push({id, normalized});
}

void VST3HostedPlugin::beginEdit(ParamID id)
{
    auto index = indexOfParamId(id);

    if (auto* listener = hostEditListener(); listener != nullptr && index >= 0)
        listener->beginParameterEdit(index);
}

void VST3HostedPlugin::performEdit(ParamID id, double normalized)
{
    auto value = static_cast<float>(normalized);

    // The controller is the one reporting it; the cache and the processor are
    // what need telling.
    if (auto* param = findParameter(id))
        param->store(value);

    pendingEdits.push({id, value});

    // Last, so a listener reading the parameter back sees the new value.
    auto index = indexOfParamId(id);

    if (auto* listener = hostEditListener(); listener != nullptr && index >= 0)
        listener->performParameterEdit(index, value);
}

void VST3HostedPlugin::endEdit(ParamID id)
{
    auto index = indexOfParamId(id);

    if (auto* listener = hostEditListener(); listener != nullptr && index >= 0)
        listener->endParameterEdit(index);
}

void VST3HostedPlugin::refreshValues()
{
    if (!controller)
        return;

    for (auto& param: storage)
        param->store(
            static_cast<float>(controller->getParamNormalized(param->paramId())));
}

bool VST3HostedPlugin::refreshInfo()
{
    if (!controller)
        return false;

    // By id, not position: nothing promises getParameterInfo's order. A plugin
    // whose parameter SET changed has to be reloaded (kReloadComponent asks for
    // exactly that); swapping parameters here would dangle every index a host
    // holds.
    auto changed = false;

    for (auto i = int32 {0}; i < controller->getParameterCount(); ++i)
    {
        auto info = ParameterInfo();

        if (controller->getParameterInfo(i, info) != kResultOk)
            continue;

        if (auto* param = findParameter(info.id))
            changed = param->updateInfo(info) || changed;
    }

    return changed;
}

void VST3HostedPlugin::restart(int32 flags)
{
    if ((flags & (kParamValuesChanged | kReloadComponent)) != 0)
    {
        refreshValues();
        notifyHostParameterValuesChanged();
    }

    // Reported only when a name moved, so a plugin restarting defensively costs
    // the host nothing.
    if ((flags & (kParamTitlesChanged | kReloadComponent)) != 0 && refreshInfo())
    {
        raiseParameterInfoChanged();
        refreshParameterNames();
    }

    if ((flags & kLatencyChanged) != 0)
    {
        if (processor)
            latency.store(static_cast<int>(processor->getLatencySamples()));

        raiseLatencyChanged();
        notifyHostLatencyChanged();
    }

    // Renegotiating buses means deactivating and preparing again, which is the
    // host's decision.
    if ((flags & kIoChanged) != 0)
        raiseIoChanged();
}

IComponentHandler* VST3HostedPlugin::componentHandlerInterface() const noexcept
{
    return handler.get();
}

std::string VST3HostedPlugin::saveState(StateContext)
{
    if (!component)
        return {};

    auto stream = MemoryStream();

    if (component->getState(&stream) != kResultOk)
        return {};

    auto size = TSize {};
    stream.seek(0, IBStream::kIBSeekEnd, &size);

    return {stream.getData(), static_cast<size_t>(size)};
}

void VST3HostedPlugin::loadState(std::string_view data, StateContext)
{
    if (!component || data.empty())
        return;

    auto stream = MemoryStream();
    auto written = int32 {};
    stream.write(
        const_cast<char*>(data.data()), static_cast<int32>(data.size()), &written);
    stream.seek(0, IBStream::kIBSeekSet, nullptr);
    component->setState(&stream);

    if (separateController)
    {
        stream.seek(0, IBStream::kIBSeekSet, nullptr);
        controller->setComponentState(&stream);
    }

    // The controller now holds values the cache has never seen.
    refreshValues();
}

// VST3Common.h, first in this TU, renames IEditController's getState and
// setState to getEditorState and setEditorState: these are the controller's.
std::string VST3HostedPlugin::saveControllerState()
{
    if (!controller)
        return {};

    auto stream = MemoryStream();

    if (controller->getEditorState(&stream) != kResultOk)
        return {};

    auto size = TSize {};
    stream.seek(0, IBStream::kIBSeekEnd, &size);

    return {stream.getData(), static_cast<size_t>(size)};
}

void VST3HostedPlugin::loadControllerState(const std::string& data)
{
    if (!controller || data.empty())
        return;

    auto stream = MemoryStream();
    auto written = int32 {};
    stream.write(
        const_cast<char*>(data.data()), static_cast<int32>(data.size()), &written);
    stream.seek(0, IBStream::kIBSeekSet, nullptr);
    controller->setEditorState(&stream);

    refreshValues();
}

} // namespace MakeASound::Hosting
