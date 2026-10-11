#include "Adapter.h"
#include "CocoaUI.h"
#include "ComponentType.h"
#include "Conversion.h"
#include "HostParameters.h"
#include "State.h"
#include "../Realtime/MessageThread.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace MakeASound::AU
{

namespace
{
constexpr auto maxPublishedChannels = 8;
constexpr auto midiPacketBytes = 16 * 1024;

const ModuleDescription& thisModule()
{
    static const auto module = describeModule();
    return module;
}

OwningPointer<Plugin> createPluginFor(AudioComponentInstance instance)
{
    auto description = AudioComponentDescription {};
    AudioComponentGetDescription(AudioComponentInstanceGetComponent(instance),
                                 &description);

    const auto* plugin = Adapter::describe(description.componentSubType);
    auto created = plugin != nullptr && plugin->create ? plugin->create()
                                                       : OwningPointer<Plugin> {};

    if (!created)
        au::Throw(kAudioUnitErr_InvalidProperty);

    return created;
}

UInt32 inputBusCount(const OwningPointer<Plugin>& plugin)
{
    return static_cast<UInt32>(plugin->getBusLayout().inputs.size());
}

UInt32 outputBusCount(const OwningPointer<Plugin>& plugin)
{
    return static_cast<UInt32>(plugin->getBusLayout().outputs.size());
}

AudioStreamBasicDescription floatFormat(int channels)
{
    return au::ASBD::CreateCommonFloat32(au::AUBase::kAUDefaultSampleRate,
                                         static_cast<UInt32>(channels));
}

bool hasCocoaView()
{
    static const auto present = []
    {
        auto info = cocoaViewInfo();

        if (info.className != nullptr)
            CFRelease(info.className);

        if (info.bundleURL != nullptr)
            CFRelease(info.bundleURL);

        return info.className != nullptr;
    }();

    return present;
}
} // namespace

Adapter::Adapter(AudioComponentInstance instance)
    : Adapter(instance, createPluginFor(instance))
{
}

Adapter::Adapter(AudioComponentInstance instance, OwningPointer<Plugin> plugin)
    : MusicDeviceBase(instance, inputBusCount(plugin), outputBusCount(plugin))
    , pluginWrapper(std::move(plugin), PluginFormat::AU)
{
    adoptHostMessageThread();
    this->plugin().setHostEditListener(this);

    const auto& list = this->plugin().parameters();

    for (auto i = 0; i < list.size(); ++i)
        if (list.isHostExposed(i))
            exposed.add(i);

    lastHostValue.resize(exposed.size());
    lastPluginValue.resize(exposed.size());

    const auto& layout = pluginWrapper.busLayout();
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

            if (this->plugin().acceptsLayout(proposed))
                channelInfos.add(
                    {static_cast<SInt16>(in), static_cast<SInt16>(out)});
        }
    }
}

Adapter::~Adapter()
{
    auto closers = std::move(viewClosers);
    viewClosers.clear();

    for (auto& closer: closers)
        closer.close();

    plugin().setHostEditListener(nullptr);
}

const PluginDescription* Adapter::describe(OSType subtype) noexcept
{
    return findPlugin(thisModule(), subtype);
}

OSType Adapter::manufacturer() noexcept
{
    return thisModule().manufacturerCode.toUint32();
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

void Adapter::PostConstructor()
{
    MusicDeviceBase::PostConstructor();

    stagedMidi.reserveAtLeast(ProcessContext::defaultMidiCapacity);

    const auto& layout = pluginWrapper.busLayout();

    for (auto bus = 0; bus < layout.inputs.size(); ++bus)
        Input(static_cast<UInt32>(bus))
            .SetStreamFormat(floatFormat(layout.inputs[bus].numChannels));

    for (auto bus = 0; bus < layout.outputs.size(); ++bus)
    {
        auto& output = Output(static_cast<UInt32>(bus));
        output.SetStreamFormat(floatFormat(layout.outputs[bus].numChannels));
        output.SetWillAllocateBuffer(true);
    }

    // Every id now, so no SetParameterRT ever inserts on the render thread.
    mirrorParametersToHost();
}

double Adapter::outputSampleRate()
{
    if (auto output = GetOutputOrError(0))
        if (auto rate = output->GetStreamFormat().mSampleRate; rate > 0.0)
            return rate;

    return au::AUBase::kAUDefaultSampleRate;
}

BusLayout Adapter::negotiatedLayout()
{
    auto layout = pluginWrapper.busLayout();

    for (auto bus = 0; bus < layout.inputs.size(); ++bus)
        layout.inputs[bus].numChannels =
            static_cast<int>(Input(static_cast<UInt32>(bus)).NumberChannels());

    for (auto bus = 0; bus < layout.outputs.size(); ++bus)
        layout.outputs[bus].numChannels =
            static_cast<int>(Output(static_cast<UInt32>(bus)).NumberChannels());

    return layout;
}

OSStatus Adapter::Initialize()
{
    auto result = MusicDeviceBase::Initialize();

    if (result != noErr)
        return result;

    if (!pluginWrapper.setLayout(negotiatedLayout()))
        return kAudioUnitErr_FormatNotSupported;

    pluginWrapper.prepare(static_cast<int>(std::lround(outputSampleRate())),
                          static_cast<int>(GetMaxFramesPerSlice()));

    const auto& layout = pluginWrapper.busLayout();

    inputTables.resize(layout.inputs.size());
    inputBound.resize(layout.inputs.size());

    for (auto bus = 0; bus < layout.inputs.size(); ++bus)
        inputTables[bus].resize(layout.inputs[bus].numChannels);

    outputTables.resize(layout.outputs.size());

    for (auto bus = 0; bus < layout.outputs.size(); ++bus)
        outputTables[bus].resize(layout.outputs[bus].numChannels);

    midiPacketBuffer.resize(layout.midiOutputs.empty() ? 0 : midiPacketBytes);

    // The host's values stay as it set them: a choice snaps in the plugin, and
    // Globals() keeps the value the host wrote.
    pushParametersToPlugin();

    const auto& list = plugin().parameters();

    for (auto slot = 0; slot < exposed.size(); ++slot)
    {
        auto index = exposed[slot];
        auto host = Globals()->GetParameterOrError(list.entry(index).hostId);
        lastPluginValue[slot] = pluginWrapper.getParameter(index);
        lastHostValue[slot] = host ? *host : lastPluginValue[slot];
    }

    resetPending = false;
    pluginWrapper.reset();

    return noErr;
}

OSStatus Adapter::Reset(AudioUnitScope scope, AudioUnitElement element)
{
    // The SDK does not serialise Reset against Render: the next block resets.
    resetPending = true;
    return MusicDeviceBase::Reset(scope, element);
}

bool Adapter::StreamFormatWritable(AudioUnitScope scope, AudioUnitElement)
{
    return !IsInitialized()
           && (scope == kAudioUnitScope_Input || scope == kAudioUnitScope_Output);
}

UInt32 Adapter::SupportedNumChannels(const AUChannelInfo** outInfo)
{
    if (outInfo != nullptr)
        *outInfo = channelInfos.empty() ? nullptr : channelInfos.data();

    return static_cast<UInt32>(channelInfos.size());
}

bool Adapter::isPublishedCount(bool input, int channels) const noexcept
{
    return std::any_of(
        channelInfos.begin(),
        channelInfos.end(),
        [&](const AUChannelInfo& info)
        { return (input ? info.inChannels : info.outChannels) == channels; });
}

bool Adapter::ValidFormat(AudioUnitScope scope,
                          AudioUnitElement element,
                          const AudioStreamBasicDescription& format)
{
    if (!MusicDeviceBase::ValidFormat(scope, element, format))
        return false;

    // Hosts set one element at a time, so each is judged alone against its side
    // of the published pairs; Initialize() judges the combination.
    const auto declared = plugin().getBusLayout();
    auto channels = static_cast<int>(format.mChannelsPerFrame);
    auto bus = static_cast<int>(element);

    if (scope == kAudioUnitScope_Input && bus < declared.inputs.size())
        return bus == 0 ? isPublishedCount(true, channels)
                        : channels == declared.inputs[bus].numChannels;

    if (scope == kAudioUnitScope_Output && bus < declared.outputs.size())
        return bus == 0 ? isPublishedCount(false, channels)
                        : channels == declared.outputs[bus].numChannels;

    return true;
}

int Adapter::indexOfExposed(AudioUnitParameterID id) const noexcept
{
    const auto& list = pluginWrapper.plugin().parameters();
    auto index = list.indexOfHostId(id);
    return list.isHostExposed(index) ? index : -1;
}

OSStatus Adapter::GetParameterList(AudioUnitScope scope,
                                   AudioUnitParameterID* outList,
                                   UInt32& outCount)
{
    if (scope != kAudioUnitScope_Global)
        return MusicDeviceBase::GetParameterList(scope, outList, outCount);

    const auto& list = plugin().parameters();
    outCount = static_cast<UInt32>(exposed.size());

    if (outList != nullptr)
        for (auto slot = 0; slot < exposed.size(); ++slot)
            outList[slot] = list.entry(exposed[slot]).hostId;

    return noErr;
}

OSStatus Adapter::GetParameterInfo(AudioUnitScope scope,
                                   AudioUnitParameterID id,
                                   AudioUnitParameterInfo& info)
{
    if (scope != kAudioUnitScope_Global)
        return kAudioUnitErr_InvalidScope;

    auto index = indexOfExposed(id);

    if (index < 0)
        return kAudioUnitErr_InvalidParameter;

    fillParameterInfo(plugin().parameters().entry(index), info);
    return noErr;
}

OSStatus Adapter::GetParameterValueStrings(AudioUnitScope scope,
                                           AudioUnitParameterID id,
                                           CFArrayRef* outStrings)
{
    if (scope != kAudioUnitScope_Global)
        return kAudioUnitErr_InvalidScope;

    auto index = indexOfExposed(id);

    if (index < 0)
        return kAudioUnitErr_InvalidParameter;

    const auto& param = plugin().parameters()[index];

    if (!hasValueStrings(param))
        return kAudioUnitErr_InvalidProperty;

    if (outStrings != nullptr)
        *outStrings = copyValueStrings(param);

    return noErr;
}

OSStatus Adapter::SetParameter(AudioUnitParameterID id,
                               AudioUnitScope scope,
                               AudioUnitElement element,
                               AudioUnitParameterValue value,
                               UInt32 bufferOffset) noexcept
{
    if (scope != kAudioUnitScope_Global)
        return MusicDeviceBase::SetParameter(
            id, scope, element, value, bufferOffset);

    auto index = indexOfExposed(id);

    if (index < 0)
        return kAudioUnitErr_InvalidParameter;

    auto result =
        MusicDeviceBase::SetParameter(id, scope, element, value, bufferOffset);

    // At once, not only at the next render: a save between the two must not
    // choose between the host's value and one the plugin moved itself.
    if (result == noErr)
    {
        pluginWrapper.setParameter(index, value);

        if (isMessageThread())
            notifyHostIfLatencyChanged();
    }

    return result;
}

OSStatus Adapter::GetPropertyInfo(AudioUnitPropertyID id,
                                  AudioUnitScope scope,
                                  AudioUnitElement element,
                                  UInt32& outDataSize,
                                  bool& outWritable)
{
    if (scope == kAudioUnitScope_Global)
    {
        auto hasMidiOut = !pluginWrapper.busLayout().midiOutputs.empty();
        outWritable = false;

        switch (id)
        {
            case adapterProperty:
                outDataSize = sizeof(Adapter*);
                return noErr;

            case kAudioUnitProperty_CocoaUI:
                if (!hasCocoaView())
                    break;

                outDataSize = sizeof(AudioUnitCocoaViewInfo);
                return noErr;

            case kAudioUnitProperty_ParameterStringFromValue:
                outDataSize = sizeof(AudioUnitParameterStringFromValue);
                return noErr;

            case kAudioUnitProperty_ParameterValueFromString:
                outDataSize = sizeof(AudioUnitParameterValueFromString);
                return noErr;

            case kAudioUnitProperty_MIDIOutputCallbackInfo:
                if (!hasMidiOut)
                    break;

                outDataSize = sizeof(CFArrayRef);
                return noErr;

            case kAudioUnitProperty_MIDIOutputCallback:
                if (!hasMidiOut)
                    break;

                outDataSize = sizeof(AUMIDIOutputCallbackStruct);
                outWritable = true;
                return noErr;

            case kAudioUnitProperty_ClassInfoFromDocument:
                outDataSize = sizeof(CFPropertyListRef);
                outWritable = true;
                return noErr;

            default:
                break;
        }
    }

    return MusicDeviceBase::GetPropertyInfo(
        id, scope, element, outDataSize, outWritable);
}

OSStatus Adapter::GetProperty(AudioUnitPropertyID id,
                              AudioUnitScope scope,
                              AudioUnitElement element,
                              void* outData)
{
    if (scope == kAudioUnitScope_Global)
    {
        switch (id)
        {
            case adapterProperty:
                *static_cast<Adapter**>(outData) = this;
                return noErr;

            case kAudioUnitProperty_CocoaUI:
            {
                auto view = cocoaViewInfo();

                if (view.className == nullptr)
                {
                    if (view.bundleURL != nullptr)
                        CFRelease(view.bundleURL);

                    break;
                }

                auto* info = static_cast<AudioUnitCocoaViewInfo*>(outData);
                info->mCocoaAUViewBundleLocation = view.bundleURL;
                info->mCocoaAUViewClass[0] = view.className;
                return noErr;
            }

            case kAudioUnitProperty_ParameterStringFromValue:
            {
                auto* request =
                    static_cast<AudioUnitParameterStringFromValue*>(outData);
                auto index = indexOfExposed(request->inParamID);

                if (index < 0)
                    return kAudioUnitErr_InvalidParameter;

                auto plain = request->inValue != nullptr
                                 ? *request->inValue
                                 : pluginWrapper.getParameter(index);
                request->outString =
                    copyTextForValue(plugin().parameters()[index], plain);
                return noErr;
            }

            case kAudioUnitProperty_ParameterValueFromString:
            {
                auto* request =
                    static_cast<AudioUnitParameterValueFromString*>(outData);
                auto index = indexOfExposed(request->inParamID);

                if (index < 0)
                    return kAudioUnitErr_InvalidParameter;

                if (request->inString == nullptr)
                    return kAudioUnitErr_InvalidPropertyValue;

                request->outValue =
                    valueForText(plugin().parameters()[index], request->inString);
                return noErr;
            }

            case kAudioUnitProperty_MIDIOutputCallbackInfo:
            {
                const auto& buses = pluginWrapper.busLayout().midiOutputs;

                if (buses.empty())
                    break;

                auto* names = CFArrayCreateMutable(
                    kCFAllocatorDefault, buses.size(), &kCFTypeArrayCallBacks);

                for (const auto& bus: buses)
                {
                    auto* name = makeCFString(bus.name);
                    CFArrayAppendValue(names, name);
                    CFRelease(name);
                }

                *static_cast<CFArrayRef*>(outData) = names;
                return noErr;
            }

            default:
                break;
        }
    }

    return MusicDeviceBase::GetProperty(id, scope, element, outData);
}

OSStatus Adapter::SetProperty(AudioUnitPropertyID id,
                              AudioUnitScope scope,
                              AudioUnitElement element,
                              const void* inData,
                              UInt32 inDataSize)
{
    if (scope == kAudioUnitScope_Global
        && id == kAudioUnitProperty_MIDIOutputCallback
        && !pluginWrapper.busLayout().midiOutputs.empty())
    {
        if (inData == nullptr || inDataSize < sizeof(AUMIDIOutputCallbackStruct))
            return kAudioUnitErr_InvalidPropertyValue;

        midiOutputCallback.publish(
            *static_cast<const AUMIDIOutputCallbackStruct*>(inData));
        return noErr;
    }

    // The SDK does not dispatch it; Logic restores a document's state through it.
    if (scope == kAudioUnitScope_Global
        && id == kAudioUnitProperty_ClassInfoFromDocument)
    {
        if (inData == nullptr || inDataSize != sizeof(CFPropertyListRef))
            return kAudioUnitErr_InvalidPropertyValue;

        return RestoreState(*static_cast<const CFPropertyListRef*>(inData));
    }

    return MusicDeviceBase::SetProperty(id, scope, element, inData, inDataSize);
}

Float64 Adapter::GetLatency() noexcept
{
    auto latency = std::max(0, plugin().latencySamples());
    lastReportedLatency = latency;
    return static_cast<Float64>(latency) / outputSampleRate();
}

Float64 Adapter::GetTailTime() noexcept
{
    return static_cast<Float64>(std::max(0, plugin().tailSamples()))
           / outputSampleRate();
}

void Adapter::notifyHostIfLatencyChanged()
{
    if (isMessageThread())
        plugin().takeLatencyChanged();

    auto reported = std::max(0, plugin().latencySamples());
    auto previous = lastReportedLatency.load();

    // Only the caller that moves the baseline tells the host, so a GetLatency()
    // racing in between is never told about a stale figure.
    do
    {
        if (previous < 0 || previous == reported)
            return;
    } while (!lastReportedLatency.compare_exchange_weak(previous, reported));

    PropertyChanged(kAudioUnitProperty_Latency, kAudioUnitScope_Global, 0);
}

void Adapter::pushParametersToPlugin()
{
    const auto& list = plugin().parameters();

    for (auto index: exposed)
        if (auto value = Globals()->GetParameterOrError(list.entry(index).hostId))
            pluginWrapper.setParameter(index, *value);
}

void Adapter::mirrorParametersToHost()
{
    const auto& list = plugin().parameters();

    for (auto index: exposed)
        std::ignore = Globals()->SetParameterOrError(
            list.entry(index).hostId, pluginWrapper.getParameter(index), true);
}

OSStatus Adapter::SaveState(CFPropertyListRef* outData)
{
    auto result = MusicDeviceBase::SaveState(outData);

    if (result != noErr || outData == nullptr)
        return result;

    try
    {
        writeState(*outData, pluginWrapper.saveState(StateContext::Session));
    }
    catch (...)
    {
    }

    return noErr;
}

OSStatus Adapter::RestoreState(CFPropertyListRef plist)
{
    auto result = MusicDeviceBase::RestoreState(plist);

    if (result != noErr)
        return result;

    try
    {
        if (auto document = readState(plist))
        {
            pluginWrapper.loadState(*document, StateContext::Session);
            mirrorParametersToHost();
        }
        else
        {
            pushParametersToPlugin();
        }

        notifyHostIfLatencyChanged();
    }
    catch (...)
    {
    }

    return noErr;
}

void Adapter::reconcileParameters() noexcept
{
    const auto& list = plugin().parameters();

    for (auto slot = 0; slot < exposed.size(); ++slot)
    {
        auto index = exposed[slot];
        auto id = list.entry(index).hostId;
        auto host = Globals()->GetParameterOrError(id);

        if (!host)
            continue;

        auto hostMoved = *host != lastHostValue[slot];

        if (hostMoved && !pluginWrapper.isParameterHeld(index))
        {
            pluginWrapper.setParameter(index, *host);
            lastHostValue[slot] = *host;
            lastPluginValue[slot] = pluginWrapper.getParameter(index);
            continue;
        }

        // The plugin moved on its own, or a held parameter's host write is undone.
        auto value = pluginWrapper.getParameter(index);

        if (hostMoved || value != lastPluginValue[slot])
        {
            if (value != *host)
                std::ignore = Globals()->SetParameterOrError(id, value);

            lastHostValue[slot] = value;
        }

        lastPluginValue[slot] = value;
    }
}

void Adapter::bindInputs(AudioUnitRenderActionFlags&,
                         const AudioTimeStamp& timestamp,
                         UInt32 frames) noexcept
{
    auto numSamples = static_cast<int>(frames);

    for (auto bus = 0; bus < inputTables.size(); ++bus)
    {
        auto pullFlags = AudioUnitRenderActionFlags {};
        inputBound[bus] = 0;

        auto input = GetInputOrError(static_cast<UInt32>(bus));
        auto pulled =
            input
            && PullInput(static_cast<UInt32>(bus), pullFlags, timestamp, frames)
                   == noErr;
        auto buffers = pulled ? input->GetBufferListOrError()
                              : au::ExpectedPtr<AudioBufferList> {};

        if (!pulled || !buffers || buffers.get() == nullptr)
        {
            pluginWrapper.bindInput(bus, nullptr, 0, numSamples);
            continue;
        }

        auto& table = inputTables[bus];
        auto channels =
            std::min(table.size(), static_cast<int>(buffers->mNumberBuffers));

        for (auto ch = 0; ch < channels; ++ch)
            table[ch] = static_cast<const float*>(buffers->mBuffers[ch].mData);

        pluginWrapper.bindInput(bus, table.data(), channels, numSamples);
        inputBound[bus] = channels == table.size() ? 1 : 0;
    }
}

void Adapter::bindOutputs(UInt32 frames) noexcept
{
    auto numSamples = static_cast<int>(frames);

    // As DoRenderBus: a lone output was already pointed at the host's buffers or
    // prepared, but with several every bus renders into the SDK's own, bus 0
    // included, and is copied out per bus afterwards.
    auto single = Outputs().GetNumberOfElements() == 1;

    for (auto bus = 0; bus < outputTables.size(); ++bus)
    {
        auto output = GetOutputOrError(static_cast<UInt32>(bus));

        if (!output)
            continue;

        auto buffers = single ? output->GetBufferListOrError()
                              : output->PrepareBufferOrError(frames);

        if (!buffers || buffers.get() == nullptr)
            continue;

        auto& table = outputTables[bus];
        auto channels =
            std::min(table.size(), static_cast<int>(buffers->mNumberBuffers));

        for (auto ch = 0; ch < channels; ++ch)
            table[ch] = static_cast<float*>(buffers->mBuffers[ch].mData);

        auto matching = static_cast<const float* const*>(nullptr);
        auto matchingChannels = 0;

        if (bus < inputTables.size() && inputBound[bus] != 0
            && inputTables[bus].size() == channels)
        {
            matching = inputTables[bus].data();
            matchingChannels = channels;
        }

        pluginWrapper.bindOutput(
            bus, table.data(), channels, numSamples, matching, matchingChannels);
    }
}

void Adapter::emitMidiOut(const AUMIDIOutputCallbackStruct* callback,
                          const AudioTimeStamp& timestamp) noexcept
{
    if (callback == nullptr || callback->midiOutputCallback == nullptr
        || midiPacketBuffer.empty())
        return;

    auto* list = reinterpret_cast<MIDIPacketList*>(midiPacketBuffer.data());
    auto capacity = static_cast<ByteCount>(midiPacketBuffer.size());
    const auto& buses = pluginWrapper.midiOut();

    for (auto bus = 0; bus < buses.size(); ++bus)
    {
        if (buses[bus].empty())
            continue;

        auto* packet = MIDIPacketListInit(list);

        auto flush = [&]
        {
            if (list->numPackets > 0)
                callback->midiOutputCallback(
                    callback->userData, &timestamp, static_cast<UInt32>(bus), list);

            packet = MIDIPacketListInit(list);
        };

        for (const auto& event: buses[bus])
        {
            auto* next = addPacket(*list, capacity, packet, event);

            if (next == nullptr)
            {
                flush();
                next = addPacket(*list, capacity, packet, event);
            }

            if (next != nullptr)
                packet = next;
        }

        flush();
    }
}

OSStatus Adapter::Render(AudioUnitRenderActionFlags& flags,
                         const AudioTimeStamp& timestamp,
                         UInt32 frames) noexcept
{
    if (resetPending.exchange(false))
        pluginWrapper.reset();

    takeStagedMidi();
    reconcileParameters();
    bindInputs(flags, timestamp, frames);
    pluginWrapper.setPlayhead(readPlayhead(*this, timestamp));
    pluginWrapper.sortMidiInByOffset();
    bindOutputs(frames);
    pluginWrapper.process();
    emitMidiOut(midiOutputCallback.currentForBlock(), timestamp);

    return noErr;
}

void Adapter::takeStagedMidi() noexcept
{
    pluginWrapper.clearMidi();

    auto lock = ScopedSpinLock(stagedMidiLock);

    for (const auto& event: stagedMidi)
        pluginWrapper.pushMidiIn(0, event);

    stagedMidi.clear();
}

void Adapter::pushMidi(const MIDI::Event& event) noexcept
{
    auto lock = ScopedSpinLock(stagedMidiLock);

    if (stagedMidi.size() < stagedMidi.capacity())
        stagedMidi.add(event);
}

OSStatus Adapter::HandleNoteOn(UInt8 channel,
                               UInt8 note,
                               UInt8 velocity,
                               UInt32 frame) noexcept
{
    pushMidi(MIDI::Event::noteOn(
        channel, note, from7Bit(velocity), static_cast<int>(frame)));
    return noErr;
}

OSStatus Adapter::HandleNoteOff(UInt8 channel,
                                UInt8 note,
                                UInt8 velocity,
                                UInt32 frame) noexcept
{
    pushMidi(MIDI::Event::noteOff(
        channel, note, from7Bit(velocity), static_cast<int>(frame)));
    return noErr;
}

OSStatus Adapter::HandleControlChange(UInt8 channel,
                                      UInt8 controller,
                                      UInt8 value,
                                      UInt32 frame) noexcept
{
    pushMidi(MIDI::Event::controlChange(
        channel, controller, from7Bit(value), static_cast<int>(frame)));
    return noErr;
}

OSStatus Adapter::HandlePitchWheel(UInt8 channel,
                                   UInt8 lsb,
                                   UInt8 msb,
                                   UInt32 frame) noexcept
{
    pushMidi(MIDI::Event::pitchBend(
        channel, fromPitchWheel(lsb, msb), static_cast<int>(frame)));
    return noErr;
}

OSStatus
    Adapter::HandleChannelPressure(UInt8 channel, UInt8 value, UInt32 frame) noexcept
{
    pushMidi(MIDI::Event::channelAftertouch(
        channel, from7Bit(value), static_cast<int>(frame)));
    return noErr;
}

OSStatus Adapter::HandlePolyPressure(UInt8 channel,
                                     UInt8 note,
                                     UInt8 value,
                                     UInt32 frame) noexcept
{
    pushMidi(MIDI::Event::polyAftertouch(
        channel, note, from7Bit(value), static_cast<int>(frame)));
    return noErr;
}

OSStatus Adapter::HandleProgramChange(UInt8 channel, UInt8 program) noexcept
{
    pushMidi(MIDI::Event::programChange(channel, program & 0x7f));
    return noErr;
}

OSStatus Adapter::HandleSysEx(const UInt8* data, UInt32 length) noexcept
{
    if (auto event = sysExEvent(data, length))
        pushMidi(*event);

    return noErr;
}

OSStatus Adapter::HandleNonNoteEvent(
    UInt8 status, UInt8 channel, UInt8 data1, UInt8 data2, UInt32 frame) noexcept
{
    constexpr auto allSoundOff = 120;
    constexpr auto resetAllControllers = 121;
    constexpr auto allNotesOff = 123;

    if ((status & 0xf0) == 0xb0
        && (data1 == allSoundOff || data1 == resetAllControllers
            || data1 == allNotesOff))
        return HandleControlChange(channel, data1, data2, frame);

    // The base drops a program change's frame too.
    if ((status & 0xf0) == 0xc0)
    {
        pushMidi(MIDI::Event::programChange(
            channel, data1 & 0x7f, static_cast<int>(frame)));
        return noErr;
    }

    return MusicDeviceBase::HandleNonNoteEvent(status, channel, data1, data2, frame);
}

void Adapter::notifyHost(int index, AudioUnitEventType type) noexcept
{
    auto event = AudioUnitEvent {};
    event.mEventType = type;
    event.mArgument.mParameter.mAudioUnit = GetComponentInstance();
    event.mArgument.mParameter.mParameterID =
        plugin().parameters().entry(index).hostId;
    event.mArgument.mParameter.mScope = kAudioUnitScope_Global;
    event.mArgument.mParameter.mElement = 0;

    AUEventListenerNotify(nullptr, nullptr, &event);
}

void Adapter::beginParameterEdit(int index) noexcept
{
    pluginWrapper.holdParameter(index);

    if (plugin().parameters().isHostExposed(index))
        notifyHost(index, kAudioUnitEvent_BeginParameterChangeGesture);
}

void Adapter::performParameterEdit(int index, float) noexcept
{
    const auto& list = plugin().parameters();

    if (list.isHostExposed(index))
    {
        std::ignore = Globals()->SetParameterOrError(
            list.entry(index).hostId, pluginWrapper.getParameter(index), true);
        notifyHost(index, kAudioUnitEvent_ParameterValueChange);
    }

    notifyHostIfLatencyChanged();
}

void Adapter::endParameterEdit(int index) noexcept
{
    if (plugin().parameters().isHostExposed(index))
        notifyHost(index, kAudioUnitEvent_EndParameterChangeGesture);

    pluginWrapper.releaseParameter(index);
}

void Adapter::latencyChanged() noexcept
{
    notifyHostIfLatencyChanged();
}

void Adapter::parameterInfoChanged() noexcept
{
    PropertyChanged(kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0);
    PropertyChanged(kAudioUnitProperty_ParameterInfo, kAudioUnitScope_Global, 0);
}

} // namespace MakeASound::AU
