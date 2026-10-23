#include "HostedAudioUnit.h"
#include "../JuceHostCompat.h"

#include <mach/mach_time.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <limits>

namespace MakeASound::Hosting
{

namespace
{
// How often the event listener delivers value changes to the main run loop.
constexpr Float32 notificationInterval = 0.02f;

std::string stringFromCF(CFStringRef text)
{
    if (text == nullptr)
        return {};

    auto length = CFStringGetLength(text);
    auto maxBytes =
        CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
    auto buffer = std::string(static_cast<size_t>(maxBytes), '\0');

    if (!CFStringGetCString(text, buffer.data(), maxBytes, kCFStringEncodingUTF8))
        return {};

    buffer.resize(std::char_traits<char>::length(buffer.c_str()));
    return buffer;
}

bool typeConsumesMidi(OSType type) noexcept
{
    return type == kAudioUnitType_MusicDevice || type == kAudioUnitType_MusicEffect
           || type == kAudioUnitType_MIDIProcessor;
}

std::string elementName(AudioUnit unit,
                        AudioUnitScope scope,
                        UInt32 element,
                        const std::string& fallback)
{
    auto name = CFStringRef {nullptr};
    auto size = UInt32 {sizeof(name)};

    if (AudioUnitGetProperty(
            unit, kAudioUnitProperty_ElementName, scope, element, &name, &size)
            != noErr
        || name == nullptr)
        return fallback;

    auto text = stringFromCF(name);
    CFRelease(name);
    return text.empty() ? fallback : text;
}

bool readInfo(AudioUnit unit, AudioUnitParameterID id, AudioUnitParameterInfo& info)
{
    info = AudioUnitParameterInfo();
    auto size = UInt32 {sizeof(info)};

    return AudioUnitGetProperty(unit,
                                kAudioUnitProperty_ParameterInfo,
                                kAudioUnitScope_Global,
                                id,
                                &info,
                                &size)
           == noErr;
}

// The info's strings, released as its flags ask once read.
struct InfoStrings
{
    std::string name;
    std::string units;
    std::vector<std::string> values;
};

std::string unitsOf(const AudioUnitParameterInfo& info)
{
    switch (info.unit)
    {
        case kAudioUnitParameterUnit_Decibels:
            return "dB";
        case kAudioUnitParameterUnit_Hertz:
            return "Hz";
        case kAudioUnitParameterUnit_Milliseconds:
            return "ms";
        case kAudioUnitParameterUnit_Seconds:
            return "s";
        case kAudioUnitParameterUnit_Percent:
        case kAudioUnitParameterUnit_EqualPowerCrossfade:
            return "%";
        case kAudioUnitParameterUnit_Cents:
        case kAudioUnitParameterUnit_AbsoluteCents:
            return "cents";
        case kAudioUnitParameterUnit_Degrees:
            return "deg";
        case kAudioUnitParameterUnit_BPM:
            return "BPM";
        case kAudioUnitParameterUnit_Beats:
            return "beats";
        case kAudioUnitParameterUnit_CustomUnit:
            return stringFromCF(info.unitName);
        default:
            return {};
    }
}

InfoStrings takeStrings(AudioUnit unit,
                        AudioUnitParameterID id,
                        const AudioUnitParameterInfo& info)
{
    auto strings = InfoStrings();

    if ((info.flags & kAudioUnitParameterFlag_HasCFNameString) != 0
        && info.cfNameString != nullptr)
        strings.name = stringFromCF(info.cfNameString);

    if (strings.name.empty())
        strings.name =
            std::string(info.name, std::char_traits<char>::length(info.name));

    strings.units = unitsOf(info);

    if (info.unit == kAudioUnitParameterUnit_Indexed)
    {
        auto values = CFArrayRef {nullptr};
        auto size = UInt32 {sizeof(values)};

        if (AudioUnitGetProperty(unit,
                                 kAudioUnitProperty_ParameterValueStrings,
                                 kAudioUnitScope_Global,
                                 id,
                                 &values,
                                 &size)
                == noErr
            && values != nullptr)
        {
            for (auto i = CFIndex {0}; i < CFArrayGetCount(values); ++i)
                strings.values.push_back(stringFromCF(
                    static_cast<CFStringRef>(CFArrayGetValueAtIndex(values, i))));

            CFRelease(values);
        }
    }

    if ((info.flags & kAudioUnitParameterFlag_CFNameRelease) != 0)
    {
        if (info.cfNameString != nullptr)
            CFRelease(info.cfNameString);

        if (info.unit == kAudioUnitParameterUnit_CustomUnit
            && info.unitName != nullptr)
            CFRelease(info.unitName);
    }

    return strings;
}

bool isAddressable(const AudioUnitParameterInfo& info) noexcept
{
    constexpr auto readOrWrite =
        kAudioUnitParameterFlag_IsReadable | kAudioUnitParameterFlag_IsWritable;

    return (info.flags & readOrWrite) != 0;
}

ParameterOptions optionsFor(AudioUnitParameterID id,
                            const AudioUnitParameterInfo& info,
                            const std::string& key)
{
    auto options = ParameterOptions();
    options.id = key;
    options.automatable = (info.flags & kAudioUnitParameterFlag_NonRealTime) == 0;
    options.hostId = id;
    return options;
}

UInt32 dataByte(float normalized) noexcept
{
    return static_cast<UInt32>(
        std::clamp(static_cast<int>(std::lround(normalized * 127.f)), 0, 127));
}
} // namespace

// --- HostedAudioUnitParameter ----------------------------------------------

HostedAudioUnitParameter::HostedAudioUnitParameter(
    HostedAudioUnit& ownerToUse,
    AudioUnitParameterID idToUse,
    const AudioUnitParameterInfo& info,
    std::string nameToUse,
    std::string unitsToUse,
    std::vector<std::string> valueStringsToUse)
    : Parameter(nameToUse, optionsFor(idToUse, info, std::to_string(idToUse)))
    , owner(ownerToUse)
    , auId(idToUse)
    , valueStrings(std::move(valueStringsToUse))
    , low(info.minValue)
    , high(info.maxValue > info.minValue ? info.maxValue : info.minValue + 1.f)
    , initial(std::clamp(info.defaultValue, low, high))
    , boolean(info.unit == kAudioUnitParameterUnit_Boolean)
    , value(initial)
{
    if (info.unit == kAudioUnitParameterUnit_Indexed)
        steps = std::max(1, static_cast<int>(std::lround(high - low)));
    else
        steps = boolean ? 1 : 0;

    setDisplayInfo(name(), {}, unitsToUse);
    refreshFromUnit();
}

void HostedAudioUnitParameter::setValue(float plain) noexcept
{
    if (!std::isfinite(plain))
        return;

    auto clamped = std::clamp(plain, low, high);
    value.store(clamped, std::memory_order_relaxed);
    owner.parameterEdited(auId, clamped);
}

float HostedAudioUnitParameter::getValue() const noexcept
{
    return value.load(std::memory_order_relaxed);
}

void HostedAudioUnitParameter::store(float plain) noexcept
{
    if (std::isfinite(plain))
        value.store(std::clamp(plain, low, high), std::memory_order_relaxed);
}

void HostedAudioUnitParameter::refreshFromUnit() noexcept
{
    auto current = AudioUnitParameterValue {};

    if (AudioUnitGetParameter(
            owner.audioUnit(), auId, kAudioUnitScope_Global, 0, &current)
        == noErr)
        store(static_cast<float>(current));
}

int HostedAudioUnitParameter::indexOf(float plain) const noexcept
{
    auto index = static_cast<int>(std::lround(plain - low));
    return index >= 0 && index < static_cast<int>(valueStrings.size()) ? index : -1;
}

std::string HostedAudioUnitParameter::valueToText(float plain) const
{
    if (auto index = indexOf(plain); index >= 0)
        return valueStrings[static_cast<size_t>(index)];

    return Parameter::valueToText(plain);
}

float HostedAudioUnitParameter::textToValue(std::string_view text) const
{
    for (auto i = size_t {0}; i < valueStrings.size(); ++i)
        if (equalsIgnoringCase(valueStrings[i], trim(text)))
            return low + static_cast<float>(i);

    return Parameter::textToValue(text);
}

bool HostedAudioUnitParameter::updateInfo(std::string nameToUse,
                                          std::string unitsToUse,
                                          std::vector<std::string> valueStringsToUse)
{
    if (nameToUse == name() && unitsToUse == label()
        && valueStringsToUse == valueStrings)
        return false;

    setDisplayInfo(nameToUse, {}, unitsToUse);
    valueStrings = std::move(valueStringsToUse);
    return true;
}

void HostedAudioUnitParameter::markScheduled() noexcept
{
    scheduled.store(true, std::memory_order_release);
}

bool HostedAudioUnitParameter::takeScheduled() noexcept
{
    return scheduled.exchange(false, std::memory_order_acq_rel);
}

// --- HostedAudioUnit -------------------------------------------------------

HostedAudioUnit::HostedAudioUnit(PluginDescription description,
                                 AudioComponentInstance instanceToUse,
                                 AudioComponentDescription componentToUse)
    : HostedPlugin(std::move(description))
    , instance(instanceToUse)
    , described(componentToUse)
    , consumesMidi(typeConsumesMidi(componentToUse.componentType))
{
    readBusLayout();
    buildParameters();
    installListeners();
    readLatencyAndTail();
}

HostedAudioUnit::~HostedAudioUnit()
{
    removeListeners();

    if (instance == nullptr)
        return;

    if (initialized)
        AudioUnitUninitialize(instance);

    AudioComponentInstanceDispose(instance);
}

void HostedAudioUnit::readBusLayout()
{
    layout = BusLayout();

    auto collect =
        [this](AudioUnitScope scope, Vector<Bus>& target, const char* label)
    {
        auto elements = UInt32 {0};
        auto size = UInt32 {sizeof(elements)};
        auto count = kAudioUnitProperty_ElementCount;

        if (AudioUnitGetProperty(instance, count, scope, 0, &elements, &size)
            != noErr)
            return;

        for (auto element = UInt32 {0}; element < elements; ++element)
        {
            auto format = AudioStreamBasicDescription();
            auto formatSize = UInt32 {sizeof(format)};
            auto known = AudioUnitGetProperty(instance,
                                              kAudioUnitProperty_StreamFormat,
                                              scope,
                                              element,
                                              &format,
                                              &formatSize)
                         == noErr;

            auto fallback = std::string(label);

            if (elements > 1)
                fallback += " " + std::to_string(element + 1);

            // An Audio Unit has no "starts off": every element after the first is
            // an aux bus, which a host feeds only when it chooses to.
            auto bus = Bus();
            bus.name = elementName(instance, scope, element, fallback);
            bus.numChannels = known ? static_cast<int>(format.mChannelsPerFrame) : 0;
            bus.isMain = element == 0;
            bus.activeByDefault = element == 0;
            target.add(bus);
        }
    };

    collect(kAudioUnitScope_Input, layout.inputs, "Input");
    collect(kAudioUnitScope_Output, layout.outputs, "Output");

    if (consumesMidi)
        layout.midiInputs.add({.name = "MIDI In"});

    // A unit with MIDI out names its outputs here, one per bus.
    auto names = CFArrayRef {nullptr};
    auto size = UInt32 {sizeof(names)};

    if (AudioUnitGetProperty(instance,
                             kAudioUnitProperty_MIDIOutputCallbackInfo,
                             kAudioUnitScope_Global,
                             0,
                             &names,
                             &size)
            == noErr
        && names != nullptr)
    {
        for (auto i = CFIndex {0}; i < CFArrayGetCount(names); ++i)
        {
            auto name = stringFromCF(
                static_cast<CFStringRef>(CFArrayGetValueAtIndex(names, i)));
            layout.midiOutputs.add({.name = name.empty() ? "MIDI Out" : name});
        }

        CFRelease(names);
    }
}

std::vector<AudioUnitParameterID> HostedAudioUnit::readParameterIds() const
{
    auto ids = std::vector<AudioUnitParameterID>();
    auto size = UInt32 {0};

    if (AudioUnitGetPropertyInfo(instance,
                                 kAudioUnitProperty_ParameterList,
                                 kAudioUnitScope_Global,
                                 0,
                                 &size,
                                 nullptr)
            != noErr
        || size == 0)
        return ids;

    ids.resize(size / sizeof(AudioUnitParameterID));

    if (AudioUnitGetProperty(instance,
                             kAudioUnitProperty_ParameterList,
                             kAudioUnitScope_Global,
                             0,
                             ids.data(),
                             &size)
        != noErr)
        return {};

    ids.resize(size / sizeof(AudioUnitParameterID));
    return ids;
}

void HostedAudioUnit::buildParameters()
{
    auto ids = readParameterIds();

    // Some units publish their list only once initialized.
    if (ids.empty() && AudioUnitInitialize(instance) == noErr)
    {
        ids = readParameterIds();
        AudioUnitUninitialize(instance);
    }

    // Per parameter JUCE's host listed: its storage index or -1, and its clump.
    auto listed = Vector<int>();
    auto clumps = Vector<Vector<int64_t>>();

    for (auto id: ids)
    {
        auto info = AudioUnitParameterInfo();

        if (!readInfo(instance, id, info))
            continue;

        auto addressable = isAddressable(info);
        auto strings = takeStrings(instance, id, info);

        listed.add(addressable ? storage.size() : -1);
        clumps.add((info.flags & kAudioUnitParameterFlag_HasClump) != 0
                       ? Vector<int64_t> {static_cast<int64_t>(info.clumpID)}
                       : Vector<int64_t>());

        if (!addressable)
            continue;

        storage.add(
            EA::makeOwned<HostedAudioUnitParameter>(*this,
                                                    id,
                                                    info,
                                                    std::move(strings.name),
                                                    std::move(strings.units),
                                                    std::move(strings.values)));
    }

    for (auto position: Compat::juceParameterTreeOrder(clumps))
        juceIndices.add(listed[position]);

    group.addAll(storage);

    for (auto& param: storage)
        byId.add(param.get());

    auto byIdOrder = [](const auto* a, const auto* b)
    { return a->parameterId() < b->parameterId(); };
    std::sort(byId.begin(), byId.end(), byIdOrder);

    setParameters(ParameterList {group, hostedPluginIdentity()});
}

HostedAudioUnitParameter*
    HostedAudioUnit::findParameter(AudioUnitParameterID id) const noexcept
{
    auto before = [](const auto* param, AudioUnitParameterID target)
    { return param->parameterId() < target; };
    auto found = std::lower_bound(byId.begin(), byId.end(), id, before);

    if (found == byId.end() || (*found)->parameterId() != id)
        return nullptr;

    return *found;
}

int HostedAudioUnit::indexOfParameterId(AudioUnitParameterID id) const noexcept
{
    return parameters().indexOf(findParameter(id));
}

constexpr AudioUnitPropertyID watchedProperties[] = {
    kAudioUnitProperty_Latency,
    kAudioUnitProperty_TailTime,
    kAudioUnitProperty_StreamFormat,
    kAudioUnitProperty_ParameterList,
    kAudioUnitProperty_ParameterInfo};

void HostedAudioUnit::installListeners()
{
    for (auto property: watchedProperties)
        AudioUnitAddPropertyListener(instance, property, &propertyChanged, this);

    listening = true;

    // The rename source: a property listener may run on any thread, and the names
    // are read back on the main run loop.
    auto context = CFRunLoopSourceContext();
    context.info = this;
    context.perform = &infoRefresh;

    if (auto* source = CFRunLoopSourceCreate(kCFAllocatorDefault, 0, &context))
    {
        mainRunLoop = CFRunLoopGetMain();
        CFRunLoopAddSource(mainRunLoop, source, kCFRunLoopCommonModes);
        infoSource.store(source, std::memory_order_release);
    }

    // The unit's own view reports values and gestures through AUEventListener
    // notifications, delivered on the main run loop.
    if (byId.empty()
        || AUEventListenerCreate(&eventArrived,
                                 this,
                                 CFRunLoopGetMain(),
                                 kCFRunLoopDefaultMode,
                                 notificationInterval,
                                 0.f,
                                 &eventListener)
               != noErr)
    {
        eventListener = nullptr;
        return;
    }

    for (auto* param: byId)
    {
        auto event = AudioUnitEvent();
        event.mArgument.mParameter.mAudioUnit = instance;
        event.mArgument.mParameter.mParameterID = param->parameterId();
        event.mArgument.mParameter.mScope = kAudioUnitScope_Global;
        event.mArgument.mParameter.mElement = 0;

        for (auto type: {kAudioUnitEvent_ParameterValueChange,
                         kAudioUnitEvent_BeginParameterChangeGesture,
                         kAudioUnitEvent_EndParameterChangeGesture})
        {
            event.mEventType = type;
            AUEventListenerAddEventType(eventListener, this, &event);
        }
    }
}

void HostedAudioUnit::removeListeners() noexcept
{
    if (eventListener != nullptr)
        AUListenerDispose(eventListener);

    eventListener = nullptr;

    if (auto* source = infoSource.exchange(nullptr, std::memory_order_acq_rel))
    {
        CFRunLoopSourceInvalidate(source);
        CFRelease(source);
    }

    if (listening && instance != nullptr)
        for (auto property: watchedProperties)
            AudioUnitRemovePropertyListenerWithUserData(
                instance, property, &propertyChanged, this);

    listening = false;
}

void HostedAudioUnit::readLatencyAndTail() noexcept
{
    auto seconds = Float64 {0.0};
    auto size = UInt32 {sizeof(seconds)};

    if (AudioUnitGetProperty(instance,
                             kAudioUnitProperty_Latency,
                             kAudioUnitScope_Global,
                             0,
                             &seconds,
                             &size)
        == noErr)
        latency.store(static_cast<int>(std::lround(seconds * sampleRate)));

    seconds = 0.0;
    size = sizeof(seconds);

    if (AudioUnitGetProperty(instance,
                             kAudioUnitProperty_TailTime,
                             kAudioUnitScope_Global,
                             0,
                             &seconds,
                             &size)
        == noErr)
        tail.store(static_cast<double>(seconds));
}

int HostedAudioUnit::latencySamples() const noexcept
{
    return latency.load();
}

int HostedAudioUnit::tailSamples() const noexcept
{
    auto samples = tail.load() * sampleRate;

    if (!std::isfinite(samples) || samples >= std::numeric_limits<int>::max())
        return std::numeric_limits<int>::max();

    return static_cast<int>(std::lround(std::max(0.0, samples)));
}

bool HostedAudioUnit::applyStreamFormat(AudioUnitScope scope,
                                        int element,
                                        int channels) const
{
    if (channels <= 0)
        return false;

    auto format = AudioStreamBasicDescription();
    format.mSampleRate = sampleRate;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags =
        static_cast<AudioFormatFlags>(kAudioFormatFlagsNativeFloatPacked)
        | static_cast<AudioFormatFlags>(kAudioFormatFlagIsNonInterleaved);
    format.mFramesPerPacket = 1;
    format.mChannelsPerFrame = static_cast<UInt32>(channels);
    format.mBitsPerChannel = 32;
    format.mBytesPerFrame = sizeof(Float32);
    format.mBytesPerPacket = sizeof(Float32);

    return AudioUnitSetProperty(instance,
                                kAudioUnitProperty_StreamFormat,
                                scope,
                                static_cast<AudioUnitElement>(element),
                                &format,
                                sizeof(format))
           == noErr;
}

bool HostedAudioUnit::acceptsLayout(const BusLayout& proposed) const
{
    // Only the main buses' channel counts can be asked for.
    if (proposed.inputs.size() != layout.inputs.size()
        || proposed.outputs.size() != layout.outputs.size())
        return false;

    for (auto i = 1; i < proposed.inputs.size(); ++i)
        if (proposed.inputs[i].numChannels != layout.inputs[i].numChannels)
            return false;

    for (auto i = 1; i < proposed.outputs.size(); ++i)
        if (proposed.outputs[i].numChannels != layout.outputs[i].numChannels)
            return false;

    auto wantedIn = proposed.inputs.empty() ? 0 : proposed.inputs[0].numChannels;
    auto wantedOut = proposed.outputs.empty() ? 0 : proposed.outputs[0].numChannels;
    auto size = UInt32 {0};

    // No SupportedNumChannels: the unit takes what it already has.
    if (AudioUnitGetPropertyInfo(instance,
                                 kAudioUnitProperty_SupportedNumChannels,
                                 kAudioUnitScope_Global,
                                 0,
                                 &size,
                                 nullptr)
            != noErr
        || size == 0)
    {
        auto currentIn = layout.inputs.empty() ? 0 : layout.inputs[0].numChannels;
        auto currentOut = layout.outputs.empty() ? 0 : layout.outputs[0].numChannels;
        return wantedIn == currentIn && wantedOut == currentOut;
    }

    auto supported = std::vector<AUChannelInfo>(size / sizeof(AUChannelInfo));

    if (AudioUnitGetProperty(instance,
                             kAudioUnitProperty_SupportedNumChannels,
                             kAudioUnitScope_Global,
                             0,
                             supported.data(),
                             &size)
        != noErr)
        return false;

    // -1 is any count, and -2 an independent one.
    auto fits = [](SInt16 declared, int wanted)
    { return declared < 0 || declared == static_cast<SInt16>(wanted); };

    for (const auto& entry: supported)
        if (fits(entry.inChannels, wantedIn) && fits(entry.outChannels, wantedOut))
            return true;

    return false;
}

void HostedAudioUnit::prepare(const ProcessSpec& spec)
{
    if (instance == nullptr)
        return;

    if (initialized)
        AudioUnitUninitialize(instance);

    initialized = false;

    if (acceptsLayout(spec.layout))
    {
        if (!layout.inputs.empty())
            layout.inputs[0].numChannels = spec.layout.inputs[0].numChannels;

        if (!layout.outputs.empty())
            layout.outputs[0].numChannels = spec.layout.outputs[0].numChannels;
    }

    // The host's rate exactly, never rounded: the stream format carries a double.
    sampleRate = spec.sampleRate > 0.0 ? spec.sampleRate : 44100.0;
    maxBlockSize = std::max(0, spec.maxBlockSize);
    renderSampleTime = 0;
    rewindFallbackPlayhead();

    // Everything process() touches, sized here.
    silence.assign(static_cast<size_t>(maxBlockSize), 0.f);
    scratch.assign(static_cast<size_t>(maxBlockSize), 0.f);
    outputLists.resize(static_cast<size_t>(layout.outputs.size()));

    for (auto i = 0; i < layout.outputs.size(); ++i)
    {
        auto channels =
            static_cast<size_t>(std::max(1, layout.outputs[i].numChannels));
        auto bytes = sizeof(AudioBufferList) + (channels - 1) * sizeof(AudioBuffer);
        outputLists[static_cast<size_t>(i)].assign(
            (bytes + sizeof(std::uint64_t) - 1) / sizeof(std::uint64_t), 0u);
    }

    auto maxFrames = static_cast<UInt32>(maxBlockSize);
    AudioUnitSetProperty(instance,
                         kAudioUnitProperty_MaximumFramesPerSlice,
                         kAudioUnitScope_Global,
                         0,
                         &maxFrames,
                         sizeof(maxFrames));

    for (auto i = 0; i < layout.inputs.size(); ++i)
        applyStreamFormat(kAudioUnitScope_Input, i, layout.inputs[i].numChannels);

    for (auto i = 0; i < layout.outputs.size(); ++i)
        applyStreamFormat(kAudioUnitScope_Output, i, layout.outputs[i].numChannels);

    auto callback = AURenderCallbackStruct();
    callback.inputProc = &renderInput;
    callback.inputProcRefCon = this;

    for (auto i = 0; i < layout.inputs.size(); ++i)
        AudioUnitSetProperty(instance,
                             kAudioUnitProperty_SetRenderCallback,
                             kAudioUnitScope_Input,
                             static_cast<AudioUnitElement>(i),
                             &callback,
                             sizeof(callback));

    if (!layout.midiOutputs.empty())
    {
        auto midiOut = AUMIDIOutputCallbackStruct {&midiOutput, this};
        AudioUnitSetProperty(instance,
                             kAudioUnitProperty_MIDIOutputCallback,
                             kAudioUnitScope_Global,
                             0,
                             &midiOut,
                             sizeof(midiOut));
    }

    auto host = HostCallbackInfo();
    host.hostUserData = this;
    host.beatAndTempoProc = &beatAndTempo;
    host.musicalTimeLocationProc = &musicalTime;
    host.transportStateProc = &transportStateV1;
    host.transportStateProc2 = &transportState;
    AudioUnitSetProperty(instance,
                         kAudioUnitProperty_HostCallbacks,
                         kAudioUnitScope_Global,
                         0,
                         &host,
                         sizeof(host));

    initialized = AudioUnitInitialize(instance) == noErr;
    readLatencyAndTail();
    refreshValues();
}

AudioBufferList* HostedAudioUnit::outputList(int bus) noexcept
{
    auto& storageForBus = outputLists[static_cast<size_t>(bus)];
    return reinterpret_cast<AudioBufferList*>(storageForBus.data());
}

void HostedAudioUnit::process(ProcessContext& context) noexcept
{
    if (!initialized)
        return;

    auto numSamples =
        context.outputs.empty() ? 0 : context.outputs[0].getNumSamples();

    if (numSamples <= 0 || numSamples > maxBlockSize)
        return;

    currentContext = &context;
    blockPlayhead = playheadFor(context.playhead, numSamples, sampleRate);

    pushMidi(context);
    scheduleParameters();

    auto time = AudioTimeStamp();
    time.mSampleTime = static_cast<Float64>(renderSampleTime);
    time.mHostTime = mach_absolute_time();
    time.mFlags = kAudioTimeStampSampleTimeValid | kAudioTimeStampHostTimeValid;

    auto bytes = static_cast<UInt32>(numSamples * sizeof(float));

    for (auto bus = 0; bus < layout.outputs.size(); ++bus)
    {
        auto channels = layout.outputs[bus].numChannels;

        if (channels <= 0)
            continue;

        auto* supplied =
            bus < context.outputs.size() && !context.outputs[bus].isEmpty()
                ? &context.outputs[bus]
                : nullptr;
        auto* list = outputList(bus);
        list->mNumberBuffers = static_cast<UInt32>(channels);

        for (auto ch = 0; ch < channels; ++ch)
        {
            auto have = supplied != nullptr && ch < supplied->getNumChannels();
            list->mBuffers[ch].mNumberChannels = 1;
            list->mBuffers[ch].mDataByteSize = bytes;
            list->mBuffers[ch].mData =
                have ? supplied->getChannelPointer(ch) : scratch.data();
        }

        auto flags = AudioUnitRenderActionFlags {0};
        AudioUnitRender(instance,
                        &flags,
                        &time,
                        static_cast<UInt32>(bus),
                        static_cast<UInt32>(numSamples),
                        list);
    }

    // The scheduled values were rendered: the message thread tells the view.
    for (auto i = 0; i < scheduledCount; ++i)
    {
        const auto& event = scheduledEvents[static_cast<size_t>(i)];

        if (auto* param = findParameter(event.parameter))
            param->markScheduled();
    }

    if (scheduledCount > 0)
        anyScheduled.store(true, std::memory_order_release);

    scheduledCount = 0;
    renderSampleTime += numSamples;
    currentContext = nullptr;
}

bool HostedAudioUnit::setParameterFromAudioThread(int index,
                                                  float normalized) noexcept
{
    if (index < 0 || index >= storage.size() || !std::isfinite(normalized)
        || scheduledCount >= scheduledCapacity || !initialized)
        return false;

    auto& param = *storage[index];
    auto plain = param.toPlain(std::clamp(normalized, 0.f, 1.f));

    auto& event = scheduledEvents[static_cast<size_t>(scheduledCount++)];
    event.scope = kAudioUnitScope_Global;
    event.element = 0;
    event.parameter = param.parameterId();
    event.eventType = kParameterEvent_Immediate;
    event.eventValues.immediate.bufferOffset = 0;
    event.eventValues.immediate.value = plain;

    param.store(plain);
    return true;
}

void HostedAudioUnit::scheduleParameters() noexcept
{
    if (scheduledCount > 0)
        AudioUnitScheduleParameters(
            instance, scheduledEvents.data(), static_cast<UInt32>(scheduledCount));
}

void HostedAudioUnit::syncEditorWithProcessor()
{
    if (!anyScheduled.exchange(false, std::memory_order_acq_rel))
        return;

    for (auto& param: storage)
    {
        if (!param->takeScheduled())
            continue;

        auto target = AudioUnitParameter {
            instance, param->parameterId(), kAudioUnitScope_Global, 0};
        AUParameterListenerNotify(eventListener, nullptr, &target);
    }
}

int HostedAudioUnit::indexForControllerIndex(int32_t controllerIndex) const noexcept
{
    if (controllerIndex < 0 || controllerIndex >= juceIndices.size())
        return -1;

    return juceIndices[static_cast<int>(controllerIndex)];
}

void HostedAudioUnit::refreshParameterValue(int index)
{
    if (index >= 0 && index < storage.size())
        storage[index]->refreshFromUnit();
}

void HostedAudioUnit::refreshValues() noexcept
{
    for (auto& param: storage)
        param->refreshFromUnit();
}

void HostedAudioUnit::parameterEdited(AudioUnitParameterID id, float plain)
{
    // Through the listener, so the unit's own view hears it; it is excluded from
    // the notification it sends.
    if (eventListener != nullptr)
    {
        auto target = AudioUnitParameter {instance, id, kAudioUnitScope_Global, 0};
        AUParameterSet(eventListener, this, &target, plain, 0);
        return;
    }

    AudioUnitSetParameter(instance, id, kAudioUnitScope_Global, 0, plain, 0);
}

void HostedAudioUnit::parameterEvent(const AudioUnitEvent& event,
                                     AudioUnitParameterValue value)
{
    auto id = event.mArgument.mParameter.mParameterID;
    auto* param = findParameter(id);

    if (param == nullptr)
        return;

    auto index = indexOfParameterId(id);
    auto* listener = hostEditListener();

    switch (event.mEventType)
    {
        case kAudioUnitEvent_ParameterValueChange:
            param->store(static_cast<float>(value));

            if (listener != nullptr)
                listener->performParameterEdit(index, param->getNormalized());
            break;

        case kAudioUnitEvent_BeginParameterChangeGesture:
            if (listener != nullptr)
                listener->beginParameterEdit(index);
            break;

        case kAudioUnitEvent_EndParameterChangeGesture:
            if (listener != nullptr)
                listener->endParameterEdit(index);
            break;

        default:
            break;
    }
}

bool HostedAudioUnit::refreshInfo()
{
    // A changed SET of parameters needs a reload; swapping parameters here would
    // dangle every index a host holds. Names and units are read back in place.
    auto ids = readParameterIds();
    std::sort(ids.begin(), ids.end());

    for (auto* param: byId)
        if (!std::binary_search(ids.begin(), ids.end(), param->parameterId()))
        {
            raiseIoChanged();
            return false;
        }

    auto changed = false;

    for (auto& param: storage)
    {
        auto info = AudioUnitParameterInfo();

        if (!readInfo(instance, param->parameterId(), info))
            continue;

        auto strings = takeStrings(instance, param->parameterId(), info);
        changed = param->updateInfo(std::move(strings.name),
                                    std::move(strings.units),
                                    std::move(strings.values))
                  || changed;
    }

    return changed;
}

void HostedAudioUnit::refreshInfoIfDirty()
{
    if (!infoDirty.exchange(false, std::memory_order_acq_rel) || !refreshInfo())
        return;

    raiseParameterInfoChanged();
    refreshParameterNames();
}

void HostedAudioUnit::pushMidi(const ProcessContext& context) noexcept
{
    if (!consumesMidi || context.midiIn.empty())
        return;

    for (const auto& event: context.midiIn[0])
    {
        auto channel = static_cast<UInt32>(event.channel & 0x0f);
        auto offset = static_cast<UInt32>(std::max(0, event.sampleOffset));

        auto send = [&](UInt32 status, UInt32 data1, UInt32 data2)
        { MusicDeviceMIDIEvent(instance, status | channel, data1, data2, offset); };

        if (const auto* on = event.asNoteOn())
            send(0x90, static_cast<UInt32>(on->pitch), dataByte(on->velocity));
        else if (const auto* off = event.asNoteOff())
            send(0x80, static_cast<UInt32>(off->pitch), dataByte(off->velocity));
        else if (const auto* poly = event.asPolyAftertouch())
            send(0xa0, static_cast<UInt32>(poly->pitch), dataByte(poly->pressure));
        else if (const auto* cc = event.asControlChange())
            send(0xb0, static_cast<UInt32>(cc->controller), dataByte(cc->value));
        else if (const auto* program = event.asProgramChange())
            send(0xc0, static_cast<UInt32>(program->program), 0);
        else if (const auto* touch = event.asChannelAftertouch())
            send(0xd0, dataByte(touch->pressure), 0);
        else if (const auto* bend = event.asPitchBend())
        {
            auto raw = std::clamp(
                static_cast<int>(std::lround((bend->value + 1.f) * 0.5f * 16383.f)),
                0,
                16383);
            send(0xe0,
                 static_cast<UInt32>(raw & 0x7f),
                 static_cast<UInt32>((raw >> 7) & 0x7f));
        }
        else if (const auto* sysEx = event.asSysEx();
                 sysEx != nullptr && sysEx->size > 0)
            MusicDeviceSysEx(
                instance, sysEx->data.data(), static_cast<UInt32>(sysEx->size));
    }
}

OSStatus HostedAudioUnit::supplyInput(int bus,
                                      UInt32 numFrames,
                                      AudioBufferList* data) noexcept
{
    if (data == nullptr)
        return noErr;

    const auto* supplied = currentContext != nullptr && bus >= 0
                                   && bus < currentContext->inputs.size()
                                   && !currentContext->inputs[bus].isEmpty()
                               ? &currentContext->inputs[bus]
                               : nullptr;

    auto frames = std::min<UInt32>(numFrames, static_cast<UInt32>(maxBlockSize));
    auto bytes = static_cast<UInt32>(frames * sizeof(float));

    for (auto i = UInt32 {0}; i < data->mNumberBuffers; ++i)
    {
        auto& buffer = data->mBuffers[i];
        auto channel = static_cast<int>(i);
        const float* source =
            supplied != nullptr && channel < supplied->getNumChannels()
                ? supplied->getChannelPointer(channel)
                : nullptr;

        // A unit that hands no memory reads ours: the caller's, or zeros.
        if (buffer.mData == nullptr)
        {
            buffer.mNumberChannels = 1;
            buffer.mData = source != nullptr ? const_cast<float*>(source)
                                             : static_cast<void*>(silence.data());
            buffer.mDataByteSize = bytes;
            continue;
        }

        auto copied = std::min(bytes, buffer.mDataByteSize);

        if (source != nullptr)
            std::memcpy(buffer.mData, source, copied);
        else
            std::memset(buffer.mData, 0, copied);
    }

    return noErr;
}

OSStatus HostedAudioUnit::renderInput(void* self,
                                      AudioUnitRenderActionFlags*,
                                      const AudioTimeStamp*,
                                      UInt32 bus,
                                      UInt32 numFrames,
                                      AudioBufferList* data)
{
    return static_cast<HostedAudioUnit*>(self)->supplyInput(
        static_cast<int>(bus), numFrames, data);
}

void HostedAudioUnit::propertyChanged(void* self,
                                      AudioUnit,
                                      AudioUnitPropertyID property,
                                      AudioUnitScope,
                                      AudioUnitElement)
{
    auto& unit = *static_cast<HostedAudioUnit*>(self);

    switch (property)
    {
        case kAudioUnitProperty_Latency:
        case kAudioUnitProperty_TailTime:
            unit.readLatencyAndTail();
            unit.raiseLatencyChanged();
            break;

        case kAudioUnitProperty_StreamFormat:
            unit.raiseIoChanged();
            break;

        case kAudioUnitProperty_ParameterList:
        case kAudioUnitProperty_ParameterInfo:
            unit.infoDirty.store(true, std::memory_order_release);

            if (auto* source = unit.infoSource.load(std::memory_order_acquire))
            {
                CFRunLoopSourceSignal(source);
                CFRunLoopWakeUp(unit.mainRunLoop);
            }
            break;

        default:
            break;
    }
}

void HostedAudioUnit::eventArrived(void* self,
                                   void*,
                                   const AudioUnitEvent* event,
                                   UInt64,
                                   AudioUnitParameterValue value)
{
    if (event != nullptr)
        static_cast<HostedAudioUnit*>(self)->parameterEvent(*event, value);
}

void HostedAudioUnit::infoRefresh(void* self)
{
    static_cast<HostedAudioUnit*>(self)->refreshInfoIfDirty();
}

OSStatus HostedAudioUnit::midiOutput(void* self,
                                     const AudioTimeStamp*,
                                     UInt32 bus,
                                     const MIDIPacketList* packets)
{
    if (packets != nullptr)
        static_cast<HostedAudioUnit*>(self)->receiveMidi(static_cast<int>(bus),
                                                         *packets);

    return noErr;
}

void HostedAudioUnit::receiveMidi(int bus, const MIDIPacketList& packets) noexcept
{
    // Only while a block renders: that is the context the events belong to.
    if (currentContext == nullptr || bus < 0
        || bus >= currentContext->midiOut.size())
        return;

    auto& target = currentContext->midiOut[bus];

    // A packet may hold several messages; the length is the status byte's.
    auto lengthOf = [](std::uint8_t status) -> int
    {
        switch (status & 0xf0)
        {
            case 0xc0:
            case 0xd0:
                return 2;
            case 0xf0:
                return 0;
            default:
                return 3;
        }
    };

    const auto* packet = &packets.packet[0];

    for (auto p = UInt32 {0}; p < packets.numPackets; ++p)
    {
        auto offset = static_cast<int>(packet->timeStamp);
        auto i = 0;

        while (i < packet->length)
        {
            auto status = packet->data[i];

            // SysEx runs to its end byte, whole within the packet.
            if (status == 0xf0)
            {
                auto end = i + 1;

                while (end < packet->length && packet->data[end] != 0xf7)
                    ++end;

                auto size = std::min(end + 1, static_cast<int>(packet->length)) - i;

                if (size <= MIDI::SysEx::maxBytes
                    && target.size() < target.capacity())
                    target.add(MIDI::Event::sysEx(&packet->data[i], size, offset));

                i += size;
                continue;
            }

            auto length = lengthOf(status);

            // A system real-time byte, or a running status we cannot place.
            if (length == 0 || (status & 0x80) == 0 || i + length > packet->length)
            {
                ++i;
                continue;
            }

            if (auto event = MIDI::convertMidi(&packet->data[i], length, offset);
                event && target.size() < target.capacity())
                target.add(*event);

            i += length;
        }

        packet = MIDIPacketNext(packet);
    }
}

OSStatus HostedAudioUnit::beatAndTempo(void* self, Float64* beat, Float64* tempo)
{
    const auto& playhead = static_cast<HostedAudioUnit*>(self)->blockPlayhead;

    if (beat != nullptr)
        *beat = playhead.ppqPosition;

    if (tempo != nullptr)
        *tempo = playhead.bpm;

    return noErr;
}

OSStatus HostedAudioUnit::musicalTime(void* self,
                                      UInt32* toNextBeat,
                                      Float32* numerator,
                                      UInt32* denominator,
                                      Float64* downBeat)
{
    auto& unit = *static_cast<HostedAudioUnit*>(self);
    const auto& playhead = unit.blockPlayhead;

    if (toNextBeat != nullptr)
    {
        auto secondsPerBeat = playhead.bpm > 0.0 ? 60.0 / playhead.bpm : 0.5;
        auto beats = std::ceil(playhead.ppqPosition) - playhead.ppqPosition;
        *toNextBeat = static_cast<UInt32>(
            std::max(0.0, beats * secondsPerBeat * unit.sampleRate));
    }

    if (numerator != nullptr)
        *numerator = static_cast<Float32>(playhead.timeSignature.numerator);

    if (denominator != nullptr)
        *denominator = static_cast<UInt32>(playhead.timeSignature.denominator);

    if (downBeat != nullptr)
        *downBeat = playhead.barStartPpq;

    return noErr;
}

OSStatus HostedAudioUnit::transportState(void* self,
                                         Boolean* playing,
                                         Boolean* recording,
                                         Boolean* changed,
                                         Float64* sample,
                                         Boolean* cycling,
                                         Float64* cycleStart,
                                         Float64* cycleEnd)
{
    const auto& playhead = static_cast<HostedAudioUnit*>(self)->blockPlayhead;

    if (playing != nullptr)
        *playing = playhead.isPlaying;

    if (recording != nullptr)
        *recording = playhead.isRecording;

    if (changed != nullptr)
        *changed = false;

    if (sample != nullptr)
        *sample = static_cast<Float64>(playhead.sampleTime);

    if (cycling != nullptr)
        *cycling = playhead.isLooping;

    if (cycleStart != nullptr)
        *cycleStart = playhead.loopStartPpq;

    if (cycleEnd != nullptr)
        *cycleEnd = playhead.loopEndPpq;

    return noErr;
}

OSStatus HostedAudioUnit::transportStateV1(void* self,
                                           Boolean* playing,
                                           Boolean* changed,
                                           Float64* sample,
                                           Boolean* cycling,
                                           Float64* cycleStart,
                                           Float64* cycleEnd)
{
    return transportState(
        self, playing, nullptr, changed, sample, cycling, cycleStart, cycleEnd);
}

std::string HostedAudioUnit::saveState(StateContext)
{
    auto classInfo = CFPropertyListRef {nullptr};
    auto size = UInt32 {sizeof(classInfo)};

    if (AudioUnitGetProperty(instance,
                             kAudioUnitProperty_ClassInfo,
                             kAudioUnitScope_Global,
                             0,
                             &classInfo,
                             &size)
            != noErr
        || classInfo == nullptr)
        return {};

    auto* data = CFPropertyListCreateData(kCFAllocatorDefault,
                                          classInfo,
                                          kCFPropertyListBinaryFormat_v1_0,
                                          0,
                                          nullptr);
    CFRelease(classInfo);

    if (data == nullptr)
        return {};

    auto result = std::string(reinterpret_cast<const char*>(CFDataGetBytePtr(data)),
                              static_cast<size_t>(CFDataGetLength(data)));
    CFRelease(data);
    return result;
}

void HostedAudioUnit::loadState(std::string_view data, StateContext)
{
    if (data.empty())
        return;

    auto* bytes = CFDataCreate(kCFAllocatorDefault,
                               reinterpret_cast<const UInt8*>(data.data()),
                               static_cast<CFIndex>(data.size()));

    if (bytes == nullptr)
        return;

    auto plist = CFPropertyListCreateWithData(
        kCFAllocatorDefault, bytes, kCFPropertyListImmutable, nullptr, nullptr);
    CFRelease(bytes);

    if (plist == nullptr)
        return;

    AudioUnitSetProperty(instance,
                         kAudioUnitProperty_ClassInfo,
                         kAudioUnitScope_Global,
                         0,
                         &plist,
                         sizeof(plist));
    CFRelease(plist);

    // Every value may have moved: the unit's view and the cache both re-read.
    auto any = AudioUnitParameter {
        instance, kAUParameterListener_AnyParameter, kAudioUnitScope_Global, 0};
    AUParameterListenerNotify(nullptr, nullptr, &any);
    refreshValues();
}

} // namespace MakeASound::Hosting
