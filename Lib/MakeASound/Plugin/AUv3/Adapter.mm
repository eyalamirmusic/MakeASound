#include "Adapter.h"
#include "Conversion.h"
#include "Parameters.h"
#include "State.h"
#include "Text.h"
#include "../AU/ComponentType.h"
#include "../AU/HostParameters.h"
#include "../Core/Identity.h"
#include "../Realtime/MessageThread.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <utility>

namespace MakeASound::AUv3
{

namespace
{
constexpr auto maxPublishedChannels = 8;
constexpr auto defaultSampleRate = 44100.0;
constexpr auto treeSyncInterval = NSEC_PER_SEC / 30;

// Raised while this thread writes the tree on the plugin's behalf, so the
// implementor's observer, which runs inside that write, leaves the plugin alone.
thread_local auto writingTree = false;

struct ScopedTreeWrite
{
    ScopedTreeWrite() { writingTree = true; }
    ~ScopedTreeWrite() { writingTree = false; }

    ScopedTreeWrite(const ScopedTreeWrite&) = delete;
    ScopedTreeWrite& operator=(const ScopedTreeWrite&) = delete;
};

const ModuleDescription& thisModule()
{
    static const auto module = describeModule();
    return module;
}

void setError(NSError** outError, OSStatus status)
{
    if (outError != nullptr)
        *outError = [NSError errorWithDomain:NSOSStatusErrorDomain
                                        code:status
                                    userInfo:nil];
}

// Float32, one buffer per channel, at any channel count: the standard format's
// initialiser refuses more than two channels without a layout.
AVAudioFormat* floatFormat(double sampleRate, int channels)
{
    auto description = AudioStreamBasicDescription {};
    description.mSampleRate = sampleRate;
    description.mFormatID = kAudioFormatLinearPCM;
    description.mFormatFlags =
        static_cast<AudioFormatFlags>(kAudioFormatFlagsNativeFloatPacked)
        | kAudioFormatFlagIsNonInterleaved;
    description.mBytesPerPacket = sizeof(float);
    description.mFramesPerPacket = 1;
    description.mBytesPerFrame = sizeof(float);
    description.mChannelsPerFrame = static_cast<UInt32>(channels);
    description.mBitsPerChannel = 32;

    return [[AVAudioFormat alloc] initWithStreamDescription:&description];
}

AudioBufferList& bufferList(Vector<std::byte>& storage) noexcept
{
    return *reinterpret_cast<AudioBufferList*>(storage.data());
}

Vector<std::byte> bufferListStorage(int channels)
{
    return Vector<std::byte>(static_cast<int>(
        offsetof(AudioBufferList, mBuffers)
        + static_cast<size_t>(std::max(1, channels)) * sizeof(AudioBuffer)));
}

// One render call's view of the plugin's buses, sized at allocate.
template <typename Pointer>
using Tables = Vector<Vector<Pointer>>;
} // namespace

// Everything the render block reaches: C++ and the host's blocks, held strongly
// here and called through unretained copies so no retain or release ever runs on
// the audio thread.
struct Adapter::Render
{
    int maxFrames = 0;
    bool multipleOutputs = false;

    Vector<Buffer> inputBuffers;
    Vector<Vector<std::byte>> inputLists;
    Tables<const float*> inputTables;
    Vector<char> inputBound;

    Vector<Buffer> outputBuffers;
    Tables<float*> outputTables;

    // With several outputs the host renders bus by bus; the block runs on the
    // first call at a new time and later buses copy out what it left.
    bool hasRendered = false;
    Float64 renderedSampleTime = 0.0;

    AUHostMusicalContextBlock musicalContextOwner = nil;
    AUHostTransportStateBlock transportStateOwner = nil;
    AUMIDIOutputEventBlock midiOutOwner = nil;

    __unsafe_unretained AUHostMusicalContextBlock musicalContext = nil;
    __unsafe_unretained AUHostTransportStateBlock transportState = nil;
    __unsafe_unretained AUMIDIOutputEventBlock midiOut = nil;
};

OwningPointer<Adapter> Adapter::create(AUAudioUnit* unit,
                                       AudioComponentDescription description,
                                       NSError** outError)
{
    const auto& module = thisModule();
    const auto* plugin =
        description.componentManufacturer == module.manufacturerCode.toUint32()
            ? AU::findPlugin(module, description.componentSubType)
            : nullptr;

    if (plugin == nullptr)
    {
        setError(outError, kAudioUnitErr_InvalidProperty);
        return {};
    }

    return create(unit, description, *plugin, outError);
}

OwningPointer<Adapter> Adapter::create(AUAudioUnit* unit,
                                       AudioComponentDescription description,
                                       const PluginDescription& plugin,
                                       NSError** outError)
{
    auto created = plugin.create ? plugin.create() : OwningPointer<Plugin> {};

    if (!created)
    {
        setError(outError, kAudioUnitErr_InvalidProperty);
        return {};
    }

    return new Adapter(unit, description, plugin, std::move(created));
}

Adapter::Adapter(AUAudioUnit* unitToUse,
                 AudioComponentDescription descriptionToUse,
                 const PluginDescription& pluginDescription,
                 OwningPointer<Plugin> plugin)
    : unit(unitToUse)
    , description(descriptionToUse)
    , pluginWrapper(std::move(plugin),
                    PluginFormat::AUv3,
                    parameterIdentityFor(pluginDescription))
    , space(pluginDescription)
    , lastPluginValue(pluginWrapper.plugin().parameters().size(), 0.f)
    , undoHostWrite(pluginWrapper.plugin().parameters().size(), false)
{
    adoptHostMessageThread();
    this->plugin().setHostEditListener(this);

    const auto& list = this->plugin().parameters();

    for (auto i = 0; i < list.size(); ++i)
    {
        lastPluginValue[i] = pluginWrapper.getParameter(i);

        if (space.isListed(list, i))
            exposed.add(i);
    }

    buildBusses();
    buildCapabilities();
    buildTree();
    startTreeTimer();
}

Adapter::~Adapter()
{
    auto closers = std::move(viewClosers);
    viewClosers.clear();

    for (auto& closer: closers)
        closer.close();

    if (treeTimer != nil)
        dispatch_source_cancel(treeTimer);

    clearTreeBlocks(tree);
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

void Adapter::addViewCloser(void* key, std::function<void()> closer)
{
    viewClosers.add({key, std::move(closer)});
}

void Adapter::removeViewCloser(void* key)
{
    viewClosers.eraseIf([key](const ViewCloser& closer)
                        { return closer.key == key; });
}

void Adapter::buildBusses()
{
    auto* owner = unit;
    const auto& layout = pluginWrapper.busLayout();

    auto make = [&](const Vector<Bus>& buses, AUAudioUnitBusType type)
    {
        auto* array = [NSMutableArray<AUAudioUnitBus*> array];

        for (auto index = 0; index < buses.size(); ++index)
        {
            const auto& declared = buses[index];
            auto* bus = [[AUAudioUnitBus alloc]
                initWithFormat:floatFormat(defaultSampleRate, declared.numChannels)
                         error:nil];

            if (bus == nil)
                continue;

            bus.name = toNSString(declared.name);
            bus.maximumChannelCount = static_cast<AUAudioChannelCount>(
                index == 0 ? std::max(maxPublishedChannels, declared.numChannels)
                           : declared.numChannels);
            [array addObject:bus];
        }

        return [[AUAudioUnitBusArray alloc] initWithAudioUnit:owner
                                                      busType:type
                                                       busses:array];
    };

    inputs = make(layout.inputs, AUAudioUnitBusTypeInput);
    outputs = make(layout.outputs, AUAudioUnitBusTypeOutput);
}

void Adapter::buildCapabilities()
{
    const auto& layout = pluginWrapper.busLayout();
    auto* pairs = [NSMutableArray<NSNumber*> array];

    auto counts = [](bool hasBus)
    {
        auto result = Vector<int> {};

        if (!hasBus)
            result.add(0);
        else
            for (auto n = 1; n <= maxPublishedChannels; ++n)
                result.add(n);

        return result;
    };

    for (auto in: counts(!layout.inputs.empty()))
    {
        for (auto out: counts(!layout.outputs.empty()))
        {
            auto proposed = layout;

            if (in > 0)
                proposed.inputs[0].numChannels = in;

            if (out > 0)
                proposed.outputs[0].numChannels = out;

            if (plugin().acceptsLayout(proposed))
            {
                [pairs addObject:@(in)];
                [pairs addObject:@(out)];
            }
        }
    }

    capabilities = pairs;
}

bool Adapter::isPublishedCount(bool input, int channels) const noexcept
{
    for (NSUInteger i = 0; i + 1 < capabilities.count; i += 2)
        if (capabilities[input ? i : i + 1].intValue == channels)
            return true;

    return false;
}

bool Adapter::shouldChangeToFormat(AVAudioFormat* format, AUAudioUnitBus* bus) const
{
    auto* owner = unit;

    if (owner == nil || owner.renderResourcesAllocated || format == nil)
        return false;

    if (format.commonFormat != AVAudioPCMFormatFloat32 || format.isInterleaved)
        return false;

    const auto declared = pluginWrapper.plugin().getBusLayout();
    auto channels = static_cast<int>(format.channelCount);

    for (NSUInteger index = 0; index < inputs.count; ++index)
        if (inputs[index] == bus)
            return index == 0 ? isPublishedCount(true, channels)
                              : channels == declared.inputs[index].numChannels;

    for (NSUInteger index = 0; index < outputs.count; ++index)
        if (outputs[index] == bus)
            return index == 0 ? isPublishedCount(false, channels)
                              : channels == declared.outputs[index].numChannels;

    return true;
}

BusLayout Adapter::negotiatedLayout() const
{
    auto layout = pluginWrapper.busLayout();

    auto channelsOf = [](AUAudioUnitBusArray* buses, int bus)
    {
        return static_cast<int>(
            buses[static_cast<NSUInteger>(bus)].format.channelCount);
    };

    for (auto bus = 0; bus < layout.inputs.size(); ++bus)
        if (static_cast<NSUInteger>(bus) < inputs.count)
            layout.inputs[bus].numChannels = channelsOf(inputs, bus);

    for (auto bus = 0; bus < layout.outputs.size(); ++bus)
        if (static_cast<NSUInteger>(bus) < outputs.count)
            layout.outputs[bus].numChannels = channelsOf(outputs, bus);

    return layout;
}

double Adapter::outputSampleRate() const
{
    auto* bus = outputs.count > 0 ? outputs[0] : inputs.count > 0 ? inputs[0] : nil;
    auto rate = bus != nil ? bus.format.sampleRate : 0.0;
    return rate > 0.0 ? rate : defaultSampleRate;
}

bool Adapter::allocateRenderResources(NSError** outError)
{
    auto* owner = unit;

    if (owner == nil || !pluginWrapper.setLayout(negotiatedLayout()))
    {
        setError(outError, kAudioUnitErr_FormatNotSupported);
        return false;
    }

    auto maxFrames = static_cast<int>(owner.maximumFramesToRender);
    pluginWrapper.prepare(static_cast<int>(std::lround(outputSampleRate())),
                          maxFrames);

    const auto& layout = pluginWrapper.busLayout();
    auto render = EA::makeOwned<Render>();
    render->maxFrames = maxFrames;
    render->multipleOutputs = layout.outputs.size() > 1;

    for (const auto& bus: layout.inputs)
    {
        render->inputBuffers.add(Buffer(bus.numChannels, maxFrames));
        render->inputLists.add(bufferListStorage(bus.numChannels));
        render->inputTables.add(Vector<const float*>(bus.numChannels));
        render->inputBound.add(0);
    }

    for (const auto& bus: layout.outputs)
    {
        render->outputBuffers.add(Buffer(bus.numChannels, maxFrames));
        render->outputTables.add(Vector<float*>(bus.numChannels));
    }

    render->musicalContextOwner = owner.musicalContextBlock;
    render->transportStateOwner = owner.transportStateBlock;
    if (!layout.midiOutputs.empty())
        render->midiOutOwner = owner.MIDIOutputEventBlock;

    render->musicalContext = render->musicalContextOwner;
    render->transportState = render->transportStateOwner;
    render->midiOut = render->midiOutOwner;

    state = std::move(render);

    resetPending = false;
    pluginWrapper.reset();
    return true;
}

void Adapter::deallocateRenderResources()
{
    state = {};
}

AUInternalRenderBlock Adapter::internalRenderBlock()
{
    // A C++ pointer: the block retains nothing and sends no message.
    auto* adapter = this;

    return ^AUAudioUnitStatus(AudioUnitRenderActionFlags* flags,
                              const AudioTimeStamp* timestamp,
                              AUAudioFrameCount frames,
                              NSInteger outputBus,
                              AudioBufferList* output,
                              const AURenderEvent* events,
                              __unsafe_unretained AURenderPullInputBlock pullInput) {
      return adapter->render(
          flags, timestamp, frames, outputBus, output, events, pullInput);
    };
}

AUAudioUnitStatus
    Adapter::render(AudioUnitRenderActionFlags* flags,
                    const AudioTimeStamp* timestamp,
                    AUAudioFrameCount frames,
                    NSInteger outputBus,
                    AudioBufferList* output,
                    const AURenderEvent* events,
                    __unsafe_unretained AURenderPullInputBlock pullInput) noexcept
{
    auto* render = state.get();

    if (render == nullptr)
        return kAudioUnitErr_Uninitialized;

    // The base class's renderBlock does not enforce maximumFramesToRender.
    if (frames > static_cast<AUAudioFrameCount>(render->maxFrames))
        return kAudioUnitErr_TooManyFramesToProcess;

    if (output == nullptr || outputBus < 0
        || outputBus >= static_cast<NSInteger>(render->outputTables.size()))
        return kAudioUnitErr_InvalidElement;

    if (!render->multipleOutputs)
    {
        renderBlock(flags, timestamp, frames, output, events, pullInput);
        return noErr;
    }

    auto time = timestamp != nullptr ? timestamp->mSampleTime : 0.0;

    if (!render->hasRendered || time != render->renderedSampleTime)
    {
        renderBlock(flags, timestamp, frames, nullptr, events, pullInput);
        render->hasRendered = true;
        render->renderedSampleTime = time;
    }

    auto& own = render->outputBuffers[static_cast<int>(outputBus)];
    auto channels =
        std::min(own.getNumChannels(), static_cast<int>(output->mNumberBuffers));
    auto bytes = static_cast<UInt32>(frames * sizeof(float));

    for (auto ch = 0; ch < channels; ++ch)
    {
        auto& buffer = output->mBuffers[ch];

        if (buffer.mData == nullptr)
            buffer.mData = own.getChannelPointer(ch);
        else if (buffer.mData != own.getChannelPointer(ch))
            std::memcpy(buffer.mData, own.getChannelPointer(ch), bytes);

        buffer.mDataByteSize = bytes;
    }

    return noErr;
}

void Adapter::renderBlock(AudioUnitRenderActionFlags*,
                          const AudioTimeStamp* timestamp,
                          AUAudioFrameCount frames,
                          AudioBufferList* singleOutput,
                          const AURenderEvent* events,
                          __unsafe_unretained AURenderPullInputBlock
                              pullInput) noexcept
{
    auto& render = *state;

    if (resetPending.exchange(false))
        pluginWrapper.reset();

    pluginWrapper.clearMidi();
    bindInputs(timestamp, frames, pullInput);
    applyEvents(events, timestamp, frames);
    pluginWrapper.setPlayhead(
        readPlayhead(render.musicalContext, render.transportState, timestamp));
    pluginWrapper.sortMidiInByOffset();
    bindOutputs(frames, singleOutput);
    pluginWrapper.process();
    emitMidiOut(timestamp);
}

void Adapter::bindInputs(const AudioTimeStamp* timestamp,
                         AUAudioFrameCount frames,
                         __unsafe_unretained AURenderPullInputBlock
                             pullInput) noexcept
{
    auto& render = *state;
    auto numSamples = static_cast<int>(frames);
    auto bytes = static_cast<UInt32>(frames * sizeof(float));

    for (auto bus = 0; bus < render.inputTables.size(); ++bus)
    {
        auto& table = render.inputTables[bus];
        auto& own = render.inputBuffers[bus];
        auto& list = bufferList(render.inputLists[bus]);
        render.inputBound[bus] = 0;

        // Our memory, which the host fills or points elsewhere.
        list.mNumberBuffers = static_cast<UInt32>(table.size());

        for (auto ch = 0; ch < table.size(); ++ch)
            list.mBuffers[ch] = {1, bytes, own.getChannelPointer(ch)};

        auto pullFlags = AudioUnitRenderActionFlags {};
        auto pulled =
            pullInput != nil && timestamp != nullptr
            && pullInput(
                   &pullFlags, timestamp, frames, static_cast<NSInteger>(bus), &list)
                   == noErr;

        if (!pulled)
        {
            pluginWrapper.bindInput(bus, nullptr, 0, numSamples);
            continue;
        }

        auto channels =
            std::min(table.size(), static_cast<int>(list.mNumberBuffers));

        for (auto ch = 0; ch < channels; ++ch)
            table[ch] = static_cast<const float*>(list.mBuffers[ch].mData);

        pluginWrapper.bindInput(bus, table.data(), channels, numSamples);
        render.inputBound[bus] = channels == table.size() ? 1 : 0;
    }
}

void Adapter::applyEvents(const AURenderEvent* events,
                          const AudioTimeStamp* timestamp,
                          AUAudioFrameCount frames) noexcept
{
    auto hasMidiIn = !pluginWrapper.busLayout().midiInputs.empty();

    for (const auto* event = events; event != nullptr; event = event->head.next)
    {
        switch (event->head.eventType)
        {
            // Written before the block, as every adapter does; a ramp lands on
            // its end value.
            case AURenderEventParameter:
            case AURenderEventParameterRamp:
            {
                auto index = indexOfExposed(event->parameter.parameterAddress);

                if (index >= 0)
                    pluginWrapper.setParameter(
                        index, plainFromTree(index, event->parameter.value));

                break;
            }

            case AURenderEventMIDI:
            case AURenderEventMIDISysEx:
            {
                if (!hasMidiIn)
                    break;

                auto offset =
                    sampleOffset(event->MIDI.eventSampleTime, timestamp, frames);

                if (auto converted = midiEvent(event->MIDI, offset))
                    pluginWrapper.pushMidiIn(0, *converted);

                break;
            }

            default:
                break;
        }
    }
}

void Adapter::bindOutputs(AUAudioFrameCount frames,
                          AudioBufferList* singleOutput) noexcept
{
    auto& render = *state;
    auto numSamples = static_cast<int>(frames);
    auto bytes = static_cast<UInt32>(frames * sizeof(float));

    for (auto bus = 0; bus < render.outputTables.size(); ++bus)
    {
        auto& table = render.outputTables[bus];
        auto& own = render.outputBuffers[bus];
        auto channels = table.size();

        // The host's buffers where it gave any; null mData asks for ours, which
        // the host then reads through the list.
        if (singleOutput != nullptr && bus == 0)
        {
            channels =
                std::min(channels, static_cast<int>(singleOutput->mNumberBuffers));

            for (auto ch = 0; ch < channels; ++ch)
            {
                auto& buffer = singleOutput->mBuffers[ch];

                if (buffer.mData == nullptr)
                    buffer.mData = own.getChannelPointer(ch);

                buffer.mDataByteSize = bytes;
                table[ch] = static_cast<float*>(buffer.mData);
            }
        }
        else
        {
            for (auto ch = 0; ch < channels; ++ch)
                table[ch] = own.getChannelPointer(ch);
        }

        auto matching = static_cast<const float* const*>(nullptr);
        auto matchingChannels = 0;

        if (bus < render.inputTables.size() && render.inputBound[bus] != 0
            && render.inputTables[bus].size() == channels)
        {
            matching = render.inputTables[bus].data();
            matchingChannels = channels;
        }

        pluginWrapper.bindOutput(
            bus, table.data(), channels, numSamples, matching, matchingChannels);
    }
}

void Adapter::emitMidiOut(const AudioTimeStamp* timestamp) noexcept
{
    auto& render = *state;

    if (render.midiOut == nil)
        return;

    auto start = timestamp != nullptr
                     ? static_cast<AUEventSampleTime>(timestamp->mSampleTime)
                     : AUEventSampleTime {};
    const auto& buses = pluginWrapper.midiOut();

    for (auto bus = 0; bus < buses.size(); ++bus)
    {
        for (const auto& event: buses[bus])
        {
            auto bytes = MIDI::toBytes(event);

            if (bytes.size > 0)
                render.midiOut(start + std::max(0, event.sampleOffset),
                               static_cast<uint8_t>(bus),
                               bytes.size,
                               bytes.data.data());
        }
    }
}

int Adapter::indexOfExposed(AUParameterAddress address) const noexcept
{
    const auto& list = pluginWrapper.plugin().parameters();
    auto index = list.indexOfAddress(static_cast<uint64_t>(address));
    return space.isListed(list, index) ? index : -1;
}

bool Adapter::isListed(int index) const noexcept
{
    return space.isListed(pluginWrapper.plugin().parameters(), index);
}

void Adapter::buildTree()
{
    auto* built = makeParameterTree(plugin().parameters(), space);

    // A C++ pointer, cleared from the tree when the adapter goes.
    auto* adapter = this;

    built.implementorValueObserver = ^(AUParameter* param, AUValue value) {
      auto index = adapter->indexOfExposed(param.address);

      if (index >= 0 && !writingTree)
          adapter->hostWrites(index, adapter->plainFromTree(index, value));
    };

    built.implementorValueProvider = ^AUValue(AUParameter* param) {
      auto index = adapter->indexOfExposed(param.address);
      return index >= 0 ? adapter->treeValueOf(index) : 0.f;
    };

    built.implementorStringFromValueCallback =
        ^NSString*(AUParameter* param, const AUValue* value) {
          auto index = adapter->indexOfExposed(param.address);

          if (index < 0)
              return @"";

          const auto& parameter = adapter->plugin().parameters()[index];
          auto plain = value != nullptr ? adapter->plainFromTree(index, *value)
                                        : adapter->pluginWrapper.getParameter(index);
          return static_cast<NSString*>(
              CFBridgingRelease(AU::copyTextForValue(parameter, plain)));
        };

    built.implementorValueFromStringCallback =
        ^AUValue(AUParameter* param, NSString* text) {
          auto index = adapter->indexOfExposed(param.address);

          if (index < 0)
              return param.minValue;

          const auto& entry = adapter->plugin().parameters().entry(index);
          return adapter->space.toHost(
              entry, AU::valueForText(*entry.param, (__bridge CFStringRef) text));
        };

    tree = built;
}

void Adapter::clearTreeBlocks(AUParameterTree* old)
{
    if (old == nil)
        return;

    old.implementorValueObserver = ^(AUParameter*, AUValue) {
    };
    old.implementorValueProvider = ^AUValue(AUParameter* param) {
      return param.minValue;
    };
    old.implementorStringFromValueCallback =
        ^NSString*(AUParameter*, const AUValue*) {
          return @"";
        };
    old.implementorValueFromStringCallback =
        ^AUValue(AUParameter* param, NSString*) {
          return param.minValue;
        };
}

void Adapter::startTreeTimer()
{
    treeTimer = dispatch_source_create(
        DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_main_queue());

    // Cancelled in the destructor, on this same main queue, so it never runs
    // past the adapter.
    auto* adapter = this;
    dispatch_source_set_event_handler(treeTimer, ^{
      adapter->syncTreeFromPlugin();
    });
    dispatch_source_set_timer(treeTimer,
                              dispatch_time(DISPATCH_TIME_NOW, treeSyncInterval),
                              treeSyncInterval,
                              treeSyncInterval / 10);
    dispatch_resume(treeTimer);
}

AUParameter* Adapter::treeParameter(int index) const
{
    const auto& list = pluginWrapper.plugin().parameters();
    return [tree parameterWithAddress:list.entry(index).address];
}

void Adapter::setTreeValue(int index, AUParameterAutomationEventType type)
{
    lastPluginValue[index] = pluginWrapper.getParameter(index);

    if (auto* parameter = treeParameter(index))
    {
        auto writing = ScopedTreeWrite {};
        [parameter setValue:treeValueOf(index)
                 originator:nil
                 atHostTime:0
                  eventType:type];
    }
}

AUValue Adapter::treeValueOf(int index) const noexcept
{
    const auto& entry = pluginWrapper.plugin().parameters().entry(index);
    return space.toHost(entry, pluginWrapper.getParameter(index));
}

float Adapter::plainFromTree(int index, AUValue value) const noexcept
{
    const auto& entry = pluginWrapper.plugin().parameters().entry(index);
    return space.fromHost(entry, value);
}

void Adapter::syncTreeFromPlugin()
{
    for (auto index: exposed)
    {
        auto undo = undoHostWrite[index].exchange(false);

        if (undo || pluginWrapper.getParameter(index) != lastPluginValue[index])
            setTreeValue(index);
    }
}

void Adapter::hostWrites(int index, float plain) noexcept
{
    auto wasBypassed = shouldBypassEffect();

    // Dropped while a gesture holds it, and then undone in the tree.
    pluginWrapper.setParameter(index, plain);

    if (pluginWrapper.isParameterHeld(index))
        undoHostWrite[index] = true;

    // The host's value stays as it set it: a choice snaps in the plugin, and the
    // next sync must not write the snapped form back over the host's.
    lastPluginValue[index] = pluginWrapper.getParameter(index);

    if (isMessageThread())
    {
        notifyHostIfLatencyChanged();

        if (shouldBypassEffect() != wasBypassed)
            notifyKeyChanged(@"shouldBypassEffect");
    }
}

NSTimeInterval Adapter::latency()
{
    auto latency = std::max(0, plugin().latencySamples());
    lastReportedLatency = latency;
    return static_cast<NSTimeInterval>(latency) / outputSampleRate();
}

NSTimeInterval Adapter::tailTime() const
{
    return static_cast<NSTimeInterval>(
               std::max(0, pluginWrapper.plugin().tailSamples()))
           / outputSampleRate();
}

void Adapter::notifyKeyChanged(NSString* key) const
{
    auto* owner = unit;
    [owner willChangeValueForKey:key];
    [owner didChangeValueForKey:key];
}

void Adapter::notifyHostIfLatencyChanged()
{
    if (isMessageThread())
        pluginWrapper.takeLatencyChanged();

    auto reported = std::max(0, plugin().latencySamples());
    auto previous = lastReportedLatency.load();

    // Only the caller that moves the baseline tells the host, so a latency read
    // racing in between is never told about a stale figure.
    do
    {
        if (previous < 0 || previous == reported)
            return;
    } while (!lastReportedLatency.compare_exchange_weak(previous, reported));

    notifyKeyChanged(@"latency");
}

bool Adapter::shouldBypassEffect() const noexcept
{
    const auto& list = pluginWrapper.plugin().parameters();
    auto index = list.bypassIndex();
    return index >= 0 && list[index].getNormalized() >= 0.5f;
}

// The parameter's path, so a gesture holding it drops the write, and the tree
// then shows what the plugin kept.
void Adapter::setShouldBypassEffect(bool bypass)
{
    const auto& list = plugin().parameters();
    auto index = list.bypassIndex();

    if (index < 0)
        return;

    pluginWrapper.setParameter(index, list[index].toPlain(bypass ? 1.f : 0.f));
    setTreeValue(index);
}

void Adapter::reset() noexcept
{
    // The base class does not serialise reset against render: the next block does.
    resetPending = true;
}

NSDictionary<NSString*, id>* Adapter::fullState(StateContext context)
{
    try
    {
        return makeState(description, pluginWrapper.saveState(context));
    }
    catch (...)
    {
        return nil;
    }
}

void Adapter::setFullState(NSDictionary<NSString*, id>* fullState,
                           StateContext context)
{
    try
    {
        if (auto document = readState(fullState, description))
        {
            pluginWrapper.loadState(*document, context);
        }
        else if (auto juceState = readJuceState(fullState, description))
        {
            // What a JUCE build saved; off the message thread it runs later, and
            // its parameterValuesChanged syncs the tree then.
            auto foreign = ForeignState {};
            foreign.origin = ForeignOrigin::JuceAUv3;
            foreign.bytes = *juceState;
            foreign.context = context;
            pluginWrapper.loadForeignState(foreign);
        }
        else if (auto plugState = readTamberPlugState(fullState, description))
        {
            // A Tamber Plug build's v2 ClassInfo, as the JUCE one above.
            auto foreign = ForeignState {};
            foreign.origin = ForeignOrigin::TamberPlugAU;
            foreign.bytes = *plugState;
            foreign.context = context;
            pluginWrapper.loadForeignState(foreign);
        }
        else
        {
            return;
        }

        // Off the message thread the timer carries the values into the tree.
        if (isMessageThread())
            syncTreeFromPlugin();

        notifyHostIfLatencyChanged();
    }
    catch (...)
    {
    }
}

NSArray<NSString*>* Adapter::midiOutputNames() const
{
    auto* names = [NSMutableArray<NSString*> array];

    for (const auto& bus: pluginWrapper.busLayout().midiOutputs)
        [names addObject:toNSString(bus.name)];

    return names;
}

void Adapter::beginParameterEdit(int index) noexcept
{
    pluginWrapper.holdParameter(index);

    if (isListed(index))
        setTreeValue(index, AUParameterAutomationEventTypeTouch);
}

void Adapter::performParameterEdit(int index, float) noexcept
{
    if (isListed(index))
        setTreeValue(index, AUParameterAutomationEventTypeValue);

    // The added bypass under a JUCE scheme is in no tree: the property alone.
    if (index >= 0 && index == plugin().parameters().bypassIndex())
        notifyKeyChanged(@"shouldBypassEffect");

    notifyHostIfLatencyChanged();
}

void Adapter::endParameterEdit(int index) noexcept
{
    if (isListed(index))
        setTreeValue(index, AUParameterAutomationEventTypeRelease);

    pluginWrapper.releaseParameter(index);
}

void Adapter::latencyChanged() noexcept
{
    notifyHostIfLatencyChanged();
}

// The tree hears of each value that moved now rather than at the next tick.
void Adapter::parameterValuesChanged() noexcept
{
    syncTreeFromPlugin();

    if (plugin().parameters().bypassIndex() >= 0)
        notifyKeyChanged(@"shouldBypassEffect");

    notifyHostIfLatencyChanged();
}

// The names, ranges or texts moved: a new tree, which the host observes by KVO.
void Adapter::parameterInfoChanged() noexcept
{
    auto* old = tree;

    [unit willChangeValueForKey:@"parameterTree"];
    buildTree();
    [unit didChangeValueForKey:@"parameterTree"];

    clearTreeBlocks(old);
}

} // namespace MakeASound::AUv3
