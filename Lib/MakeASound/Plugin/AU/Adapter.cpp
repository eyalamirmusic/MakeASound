#include "Adapter.h"
#include "CocoaUI.h"
#include "ComponentType.h"
#include "Conversion.h"
#include "HostParameters.h"
#include "State.h"
#include "../Core/Identity.h"
#include "../Realtime/MessageThread.h"

#include <dispatch/dispatch.h>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <limits>
#include <mutex>
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

const PluginDescription& descriptionOf(AudioComponentInstance instance)
{
    auto description = AudioComponentDescription {};
    AudioComponentGetDescription(AudioComponentInstanceGetComponent(instance),
                                 &description);

    const auto* plugin = Adapter::describe(description.componentSubType);

    if (plugin == nullptr)
        au::Throw(kAudioUnitErr_InvalidProperty);

    return *plugin;
}

// Its format set at once: the bus counts the base class takes come from its
// layout, before the wrapper exists.
OwningPointer<Plugin> createPlugin(const PluginDescription& plugin)
{
    setCurrentPluginFormat(PluginFormat::AU);
    auto created = plugin.create ? plugin.create() : OwningPointer<Plugin> {};

    if (!created)
        au::Throw(kAudioUnitErr_InvalidProperty);

    created->setFormat(PluginFormat::AU);
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

// JUCE's AU check (juce_AudioProcessor.cpp, validateParameter), once per process:
// a parameter with no versionHint has no place in the list a later release can
// keep. The bypass the wrapper adds is exempt, since its hint is the framework's,
// and so is an index profile (JUCELegacyIndex), whose indexed entries list by
// index and whose post-port ones are hinted by definition.
void assertVersionHints(const ParameterList& list, const ParameterSpace& space)
{
    static auto asserted = std::atomic<bool> {false};

    if (space.identity().paramIds == ParamIdRule::Index)
        return;

    auto bypass = list.bypassIndex();
    auto added = bypass >= 0 && list.entry(bypass).addedBypass;
    auto unhinted = space.firstUnhinted(list, added ? bypass : -1);

    // Give every parameter ParameterOptions::versionHint (1 for the first
    // release), so Logic's automation survives a parameter added later.
    if (unhinted >= 0 && !asserted.exchange(true))
        assert(unhinted < 0 && "A parameter has no ParameterOptions::versionHint");
}

// kAudioUnitProperty_ElementName, which the SDK also saves in ClassInfo's
// "element-name": the bus's name, as JUCE's AU names each element after its bus
// (AU_1.mm syncAudioUnitWithChannelSet), an empty one included. JUCE names only
// an enabled bus, so one with no channels stays unnamed and the host draws its
// own "Input #2".
void nameElement(au::AUElement& element, const Bus& bus)
{
    if (bus.numChannels <= 0)
        return;

    auto name = makeCFString(bus.name);
    element.SetName(name);
    CFRelease(name);
}

constexpr auto valueTimerInterval = static_cast<int64_t>(NSEC_PER_SEC / 30);
} // namespace

class Adapter::ValueTimer
{
public:
    explicit ValueTimer(Adapter& owner)
        : source(dispatch_source_create(
              DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_main_queue()))
    {
        if (source == nullptr)
            return;

        auto shared = std::make_unique<Target>();
        shared->adapter = &owner;
        target = shared.release();

        // The cancel handler owns the target from here, so a handler already
        // queued when the timer is cancelled still finds it.
        dispatch_set_context(source, target);
        dispatch_source_set_event_handler_f(source, &fire);
        dispatch_source_set_cancel_handler_f(
            source, [](void* context) { delete static_cast<Target*>(context); });
        dispatch_source_set_timer(
            source,
            dispatch_time(DISPATCH_TIME_NOW, valueTimerInterval),
            static_cast<uint64_t>(valueTimerInterval),
            static_cast<uint64_t>(valueTimerInterval / 10));
        dispatch_resume(source);
    }

    // The adapter is let go under the lock, so a handler running on the main
    // queue while another thread disposes the unit finishes first.
    ~ValueTimer()
    {
        if (source == nullptr)
            return;

        {
            auto lock = std::lock_guard {target->lock};
            target->adapter = nullptr;
        }

        dispatch_source_cancel(source);
        dispatch_release(source);
    }

    ValueTimer(const ValueTimer&) = delete;
    ValueTimer& operator=(const ValueTimer&) = delete;

private:
    struct Target
    {
        std::mutex lock;
        Adapter* adapter = nullptr;
    };

    static void fire(void* context)
    {
        auto* shared = static_cast<Target*>(context);
        auto lock = std::lock_guard {shared->lock};

        if (shared->adapter != nullptr)
            shared->adapter->postMovedParameters();
    }

    dispatch_source_t source = nullptr;
    Target* target = nullptr;
};

Adapter::Adapter(AudioComponentInstance instance)
    : Adapter(instance, descriptionOf(instance))
{
}

Adapter::Adapter(AudioComponentInstance instance,
                 const PluginDescription& description)
    : Adapter(instance, description, createPlugin(description))
{
}

Adapter::Adapter(AudioComponentInstance instance,
                 const PluginDescription& description,
                 OwningPointer<Plugin> plugin)
    : MusicDeviceBase(instance, inputBusCount(plugin), outputBusCount(plugin))
    , pluginWrapper(
          std::move(plugin), PluginFormat::AU, parameterIdentityFor(description))
    , space(description)
{
    // No setHostName(): the AudioUnit API never names its host, so hostName()
    // stays empty. hostProcessName() is the plugin's fallback.
    adoptHostMessageThread();
    this->plugin().setHostEditListener(this);

    const auto& list = this->plugin().parameters();

    for (auto i = 0; i < list.size(); ++i)
        if (space.isListed(list, i))
            exposed.add(i);

    for (auto index: space.listOrder(list))
        listOrder.add(list.entry(index).hostId);

    sdkIndexedList = space.usesSdkIndexedList(list);

    assertVersionHints(list, space);

    lastHostValue.resize(exposed.size());
    lastPluginValue.resize(exposed.size());
    movedByPlugin =
        std::make_unique<std::atomic<bool>[]>(static_cast<size_t>(exposed.size()));
    toldHost =
        std::make_unique<std::atomic<float>[]>(static_cast<size_t>(exposed.size()));
    for (auto i = 0; i < list.size(); ++i)
        slotOfIndex.add(-1);

    for (auto slot = 0; slot < exposed.size(); ++slot)
        slotOfIndex[exposed[slot]] = slot;

    if (!exposed.empty())
        valueTimer = std::make_unique<ValueTimer>(*this);

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
    valueTimer.reset();

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
    {
        auto& input = Input(static_cast<UInt32>(bus));
        input.SetStreamFormat(floatFormat(layout.inputs[bus].numChannels));
        nameElement(input, layout.inputs[bus]);
    }

    for (auto bus = 0; bus < layout.outputs.size(); ++bus)
    {
        auto& output = Output(static_cast<UInt32>(bus));
        output.SetStreamFormat(floatFormat(layout.outputs[bus].numChannels));
        output.SetWillAllocateBuffer(true);
        nameElement(output, layout.outputs[bus]);
    }

    // JUCE_FORCE_USE_LEGACY_PARAM_IDS: the SDK's own indexed list, as JUCE's
    // addParameters, the listed entries being exactly indices 0..n-1 (the added
    // bypass, unlisted, is last). Not once a post-port entry is listed, whose
    // id is no index.
    if (sdkIndexedList)
    {
        Globals()->UseIndexedParameters(static_cast<UInt32>(exposed.size()));

        for ([[maybe_unused]] auto slot = 0; slot < exposed.size(); ++slot)
            assert(plugin().parameters().entry(exposed[slot]).hostId
                       == static_cast<uint32_t>(slot)
                   && "JUCELegacyIndex lists every entry but the added bypass");
    }

    // Every id now, so no SetParameterRT ever inserts on the render thread.
    mirrorParametersToHost();
    markAllToldHost();
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

    pluginWrapper.prepare(outputSampleRate(),
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
    markAllToldHost();

    const auto& list = plugin().parameters();

    for (auto slot = 0; slot < exposed.size(); ++slot)
    {
        auto index = exposed[slot];
        auto host = Globals()->GetParameterOrError(list.entry(index).hostId);
        lastPluginValue[slot] = pluginWrapper.getParameter(index);
        lastHostValue[slot] = host ? *host : hostValueOf(index);
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
    return space.isListed(list, index) ? index : -1;
}

bool Adapter::isListed(int index) const noexcept
{
    return space.isListed(pluginWrapper.plugin().parameters(), index);
}

OSStatus Adapter::GetParameterList(AudioUnitScope scope,
                                   AudioUnitParameterID* outList,
                                   UInt32& outCount)
{
    if (scope != kAudioUnitScope_Global || sdkIndexedList)
        return MusicDeviceBase::GetParameterList(scope, outList, outCount);

    outCount = static_cast<UInt32>(listOrder.size());

    if (outList != nullptr)
        std::copy(listOrder.begin(), listOrder.end(), outList);

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

    const auto& entry = plugin().parameters().entry(index);
    space.fillInfo(entry, info);
    space.addClump(entry, info);
    return noErr;
}

OSStatus Adapter::CopyClumpName(AudioUnitScope scope,
                                UInt32 clumpId,
                                UInt32,
                                CFStringRef* outClumpName)
{
    if (scope != kAudioUnitScope_Global)
        return kAudioUnitErr_InvalidScope;

    auto* name = space.copyClumpName(plugin().parameters(), clumpId);

    if (name == nullptr)
        return kAudioUnitErr_InvalidPropertyValue;

    *outClumpName = name;
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

    const auto& entry = plugin().parameters().entry(index);

    if (!space.hasValueStrings(entry, AUFlavour::V2))
        return kAudioUnitErr_InvalidProperty;

    if (outStrings != nullptr)
        *outStrings = space.copyValueStrings(entry, AUFlavour::V2);

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

    auto wasBypassed = isBypassed();
    auto result =
        MusicDeviceBase::SetParameter(id, scope, element, value, bufferOffset);

    // At once, not only at the next render: a save between the two must not
    // choose between the host's value and one the plugin moved itself.
    if (result == noErr)
    {
        setFromHost(index, value);
        markToldHost(slotOfIndex[index]);

        if (isMessageThread())
        {
            notifyHostIfLatencyChanged();

            if (isBypassed() != wasBypassed)
                PropertyChanged(
                    kAudioUnitProperty_BypassEffect, kAudioUnitScope_Global, 0);
        }
    }

    return result;
}

bool Adapter::isBypassed() const noexcept
{
    const auto& list = pluginWrapper.plugin().parameters();
    auto index = list.bypassIndex();
    return index >= 0 && list[index].getNormalized() >= 0.5f;
}

// SetParameter's path, so a gesture holding the parameter drops the write; the
// SDK posts the property itself once SetProperty succeeds. A bypass the host does
// not list (the added one under a JUCE scheme, as JUCE's AU lists none) is the
// property alone.
OSStatus Adapter::setBypass(bool on) noexcept
{
    const auto& list = plugin().parameters();
    auto index = list.bypassIndex();
    auto plain = list[index].toPlain(on ? 1.f : 0.f);

    if (!isListed(index))
    {
        pluginWrapper.setParameter(index, plain);
        return noErr;
    }

    auto result =
        MusicDeviceBase::SetParameter(list.entry(index).hostId,
                                      kAudioUnitScope_Global,
                                      0,
                                      space.toHost(list.entry(index), plain),
                                      0);

    if (result != noErr)
        return result;

    pluginWrapper.setParameter(index, plain);
    markToldHost(slotOfIndex[index]);
    notifyHost(index, kAudioUnitEvent_ParameterValueChange);
    return noErr;
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

            case kAudioUnitProperty_BypassEffect:
                if (plugin().parameters().bypassIndex() < 0)
                    break;

                outDataSize = sizeof(UInt32);
                outWritable = true;
                return noErr;

            case kAudioUnitProperty_OfflineRender:
                outDataSize = sizeof(UInt32);
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

                const auto& entry = plugin().parameters().entry(index);
                auto plain = request->inValue != nullptr
                                 ? space.fromHost(entry, *request->inValue)
                                 : pluginWrapper.getParameter(index);
                request->outString = copyTextForValue(*entry.param, plain);
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

                const auto& entry = plugin().parameters().entry(index);
                request->outValue = space.toHost(
                    entry, valueForText(*entry.param, request->inString));
                return noErr;
            }

            case kAudioUnitProperty_BypassEffect:
                if (plugin().parameters().bypassIndex() < 0)
                    break;

                *static_cast<UInt32*>(outData) = isBypassed() ? 1 : 0;
                return noErr;

            case kAudioUnitProperty_OfflineRender:
                *static_cast<UInt32*>(outData) =
                    offlineRender.load(std::memory_order_relaxed) ? 1 : 0;
                return noErr;

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

void Adapter::PropertyChanged(AudioUnitPropertyID id,
                              AudioUnitScope scope,
                              AudioUnitElement element)
{
    MusicDeviceBase::PropertyChanged(id, scope, element);

    if (id != kAudioUnitProperty_ContextName || scope != kAudioUnitScope_Global)
        return;

    try
    {
        pluginWrapper.setTrackInfo(
            TrackInfo {.isValid = true, .name = toUtf8(GetContextName())});
    }
    catch (...)
    {
    }
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

    if (scope == kAudioUnitScope_Global && id == kAudioUnitProperty_BypassEffect
        && plugin().parameters().bypassIndex() >= 0)
    {
        if (inData == nullptr || inDataSize < sizeof(UInt32))
            return kAudioUnitErr_InvalidPropertyValue;

        return setBypass(*static_cast<const UInt32*>(inData) != 0);
    }

    if (scope == kAudioUnitScope_Global && id == kAudioUnitProperty_OfflineRender)
    {
        if (inData == nullptr || inDataSize < sizeof(UInt32))
            return kAudioUnitErr_InvalidPropertyValue;

        offlineRender.store(*static_cast<const UInt32*>(inData) != 0,
                            std::memory_order_relaxed);
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
    auto tail = plugin().tailSamples();

    if (tail == Plugin::infiniteTail)
        return std::numeric_limits<Float64>::infinity();

    return static_cast<Float64>(std::max(0, tail)) / outputSampleRate();
}

void Adapter::notifyHostIfLatencyChanged()
{
    if (isMessageThread())
        pluginWrapper.takeLatencyChanged();

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

    // A value the plugin already reports is left alone: the host-space round
    // trip can move a plain value by an ulp, and an untouched default must stay
    // the plugin's own, bit for bit, as under JUCE's AU, which pushes nothing.
    for (auto index: exposed)
        if (auto value = Globals()->GetParameterOrError(list.entry(index).hostId))
            if (*value != hostValueOf(index))
                setFromHost(index, *value);
}

void Adapter::mirrorParametersToHost()
{
    const auto& list = plugin().parameters();

    for (auto index: exposed)
        std::ignore = Globals()->SetParameterOrError(
            list.entry(index).hostId, hostValueOf(index), true);
}

float Adapter::hostValueOf(int index) const noexcept
{
    const auto& entry = pluginWrapper.plugin().parameters().entry(index);
    return space.toHost(entry, pluginWrapper.getParameter(index));
}

void Adapter::setFromHost(int index, float hostValue) noexcept
{
    const auto& entry = pluginWrapper.plugin().parameters().entry(index);
    pluginWrapper.setParameter(index, space.fromHost(entry, hostValue));
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
    auto document = std::optional<std::string> {};
    auto foreignBytes = std::optional<std::string> {};
    auto foreignOrigin = ForeignOrigin::JuceAU;
    auto wasBypassed = isBypassed();

    try
    {
        document = readState(plist);

        if (!document)
            foreignBytes = readJuceState(plist);

        if (!document && !foreignBytes)
        {
            foreignBytes = readTamberPlugState(plist);
            foreignOrigin = ForeignOrigin::TamberPlugAU;
        }
    }
    catch (...)
    {
    }

    // Another framework's dictionary (JUCE's, Plug's) loses its parameter data,
    // whose ids are not ours; an AUAudioUnit's own state (the AUv3's on an iPad)
    // carries version 1.
    auto normalized = foreignBytes ? copyWithoutParameterData(plist)
                                   : copyWithStateVersionZero(plist);

    if (normalized)
        plist = normalized.get();

    auto result = MusicDeviceBase::RestoreState(plist);

    if (result != noErr)
        return result;

    try
    {
        if (document)
        {
            pluginWrapper.loadState(*document, StateContext::Session);
            mirrorParametersToHost();
        }
        else if (foreignBytes)
        {
            auto foreign = ForeignState {};
            foreign.origin = foreignOrigin;
            foreign.bytes = *foreignBytes;
            foreign.context = StateContext::Session;

            // Off the message thread the load runs later, and its
            // parameterValuesChanged mirrors again; Plug's document, where the
            // identity makes it native, sets the parameters now, as ours does.
            pluginWrapper.loadForeignState(foreign);
            mirrorParametersToHost();
        }
        else
        {
            pushParametersToPlugin();
        }

        // Now, from the restoring thread, as JUCE's AU posts its values from
        // setStateInformation: each event reads the restored value. Off the
        // message thread the rest of the load runs later, and whatever it
        // reports the host has by then is not posted again, since that event
        // could land after a newer host write and undo it in the host's view.
        postUntoldValues();

        if (isBypassed() != wasBypassed)
            PropertyChanged(
                kAudioUnitProperty_BypassEffect, kAudioUnitScope_Global, 0);

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
            setFromHost(index, *host);
            lastHostValue[slot] = *host;
            lastPluginValue[slot] = pluginWrapper.getParameter(index);
            markToldHost(slot);
            continue;
        }

        // The plugin moved on its own, or a held parameter's host write is undone.
        auto value = pluginWrapper.getParameter(index);

        if (hostMoved || value != lastPluginValue[slot])
        {
            auto hostValue = space.toHost(list.entry(index), value);

            // Listeners hear of it from the message thread; a held parameter is
            // the editor's gesture, which reports its own values.
            if (hostValue != *host)
            {
                std::ignore = Globals()->SetParameterOrError(id, hostValue);

                if (!pluginWrapper.isParameterHeld(index))
                    movedByPlugin[slot].store(true, std::memory_order_relaxed);
            }

            lastHostValue[slot] = hostValue;
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

        auto& table = inputTables[bus];

        // Every channel absent: the wrapper hands the plugin silence of the
        // bus's width.
        if (!pulled || !buffers || buffers.get() == nullptr)
        {
            std::fill(table.begin(), table.end(), nullptr);
            pluginWrapper.bindInput(bus, table.data(), table.size(), numSamples);
            continue;
        }

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
        auto& table = outputTables[bus];

        // No buffers (an output the host told not to allocate, and is not
        // rendering now): every channel absent, so the plugin writes scratch of
        // the bus's width. A null mData among real ones is the wrapper's too.
        if (!buffers || buffers.get() == nullptr)
        {
            std::fill(table.begin(), table.end(), nullptr);
            pluginWrapper.bindOutput(bus, table.data(), table.size(), numSamples);
            continue;
        }

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
    pluginWrapper.setOffline(offlineRender.load(std::memory_order_relaxed));
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

void Adapter::markToldHost(int slot) noexcept
{
    if (slot >= 0 && slot < exposed.size())
        toldHost[slot].store(pluginWrapper.getParameter(exposed[slot]),
                             std::memory_order_relaxed);
}

void Adapter::markAllToldHost() noexcept
{
    for (auto slot = 0; slot < exposed.size(); ++slot)
        markToldHost(slot);
}

bool Adapter::postUntoldValues() noexcept
{
    const auto& list = plugin().parameters();
    auto any = false;

    for (auto slot = 0; slot < exposed.size(); ++slot)
    {
        auto index = exposed[slot];
        auto value = pluginWrapper.getParameter(index);
        auto moved = movedByPlugin[slot].exchange(false, std::memory_order_relaxed);

        if (!moved && value == toldHost[slot].load(std::memory_order_relaxed))
            continue;

        any = true;
        std::ignore = Globals()->SetParameterOrError(
            list.entry(index).hostId, space.toHost(list.entry(index), value), true);
        toldHost[slot].store(value, std::memory_order_relaxed);
        notifyHost(index, kAudioUnitEvent_ParameterValueChange);
    }

    return any;
}

void Adapter::beginParameterEdit(int index) noexcept
{
    pluginWrapper.holdParameter(index);

    if (isListed(index))
        notifyHost(index, kAudioUnitEvent_BeginParameterChangeGesture);
}

void Adapter::performParameterEdit(int index, float) noexcept
{
    const auto& list = plugin().parameters();

    if (isListed(index))
    {
        std::ignore = Globals()->SetParameterOrError(
            list.entry(index).hostId, hostValueOf(index), true);
        markToldHost(slotOfIndex[index]);
        notifyHost(index, kAudioUnitEvent_ParameterValueChange);
    }

    if (index >= 0 && index == list.bypassIndex())
        PropertyChanged(kAudioUnitProperty_BypassEffect, kAudioUnitScope_Global, 0);

    notifyHostIfLatencyChanged();
}

void Adapter::endParameterEdit(int index) noexcept
{
    if (isListed(index))
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

void Adapter::postMovedParameters() noexcept
{
    const auto bypass = plugin().parameters().bypassIndex();
    auto any = false;

    for (auto slot = 0; slot < exposed.size(); ++slot)
    {
        if (!movedByPlugin[slot].exchange(false, std::memory_order_relaxed))
            continue;

        any = true;
        markToldHost(slot);
        notifyHost(exposed[slot], kAudioUnitEvent_ParameterValueChange);

        if (exposed[slot] == bypass)
            PropertyChanged(
                kAudioUnitProperty_BypassEffect, kAudioUnitScope_Global, 0);
    }

    if (any)
        notifyHostIfLatencyChanged();
}

// Only the values the host does not have: a plugin reports a load the host made
// off the message thread once the rest of it has run here, by when the host has
// read the values and may have written newer ones (pluginval writes, restores and
// reads back, parameter by parameter). The next render takes Globals() as a host
// write of the same values.
void Adapter::parameterValuesChanged() noexcept
{
    postUntoldValues();

    if (plugin().parameters().bypassIndex() >= 0)
        PropertyChanged(kAudioUnitProperty_BypassEffect, kAudioUnitScope_Global, 0);

    notifyHostIfLatencyChanged();
}

void Adapter::stateChanged() noexcept
{
    PropertyChanged(kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0);
}

} // namespace MakeASound::AU
