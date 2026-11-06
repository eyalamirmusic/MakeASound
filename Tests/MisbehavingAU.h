#pragma once

// An Audio Unit that misbehaves, or behaves within the contract in ways a
// careless host trips over, made by hand over the component C API so each
// behaviour can be switched on alone: a unit that lists its parameters, and
// answers for their info, only once initialized (as Soundtoys' Decapitator does,
// which crashes on an info read while uninitialized); one that tells its
// listeners from inside its own Initialize and Render, and cannot be read back
// from there; one that turns a stream format down; one that pulls more input
// than the block, or pulls into the host's memory and writes there; and
// parameter info with a cached name it never asked the host to release, a name
// with no terminator, and an id listed twice.
//
// It records what a host does wrong and turns the call down with an error, never
// crashing, so a test fails on its own check. Its Render allocates nothing, for
// the allocation suite. Registered in this process and its component handed to
// the loader, never found through the registry.

#include <MakeASound/Hosting/MakeASoundHosting.h>
#include <MakeASound/Hosting/AU/AUFormat.h>
#include <MakeASound/Hosting/AU/HostedAudioUnit.h>
#include <MakeASound/Plugin/Core/Description.h>

#include <NanoTest/NanoTest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace MisbehavingAU
{
using namespace MakeASound;
using namespace MakeASound::Hosting;

constexpr auto numParameters = 3;
constexpr auto pullSentinel = -3.f;

// What the unit is set up to do, before the host first reads it.
struct Behaviour
{
    std::vector<AudioUnitParameterID> ids {0, 1, 2};

    // As Decapitator does: no parameter list until initialized, and info asked
    // for while uninitialized is a violation the unit turns down...
    bool listOnlyOnceInitialized = false;

    // ...or, still recorded, answers, so the host has parameters to refresh.
    bool answerInfoWhileUninitialized = false;

    // Told to the listeners from inside Initialize, or the first Render (every
    // Render, with tellLatencyOnEveryRender), when not negative: the unit's new
    // latency in seconds.
    Float64 latencyOnInitialize = -1.0;
    Float64 latencyOnFirstRender = -1.0;
    bool tellLatencyOnEveryRender = false;

    // SupportedNumChannels says any count, but the output takes only this many
    // (0: any).
    bool anyChannelCount = false;
    int onlyOutputChannels = 0;

    // Every Render pulls MaximumFramesPerSlice frames of input, not the block's.
    bool pullMaxFrames = false;

    // Every Render pulls input with no memory of its own, checks it is silence
    // when no input reaches the host, and writes into what it was given.
    bool pullIntoHostMemory = false;

    // Parameter 0 hands out the unit's own cached name in cfNameString with
    // CFNameRelease but without HasCFNameString, so the host must not touch it.
    bool cachedNameWithoutFlag = false;

    // Parameter 1's name fills all 52 bytes with no terminator, without
    // HasCFNameString, and the fields after it hold no zero byte.
    bool unterminatedName = false;
};

// What the host did wrong, as the unit saw it.
struct Record
{
    int initializeCalls = 0;
    int initializesWithoutHostCallbacks = 0;
    int uninitializedInfoReads = 0;
    int reentrantReads = 0;
    int renders = 0;
    int inconsistentRenders = 0;
    int pulls = 0;
    int samplesFromBeyondTheBlock = 0;
    int nonSilentSamples = 0;
};

struct PropertyListener
{
    AudioUnitPropertyID property = 0;
    AudioUnitPropertyListenerProc proc = nullptr;
    void* userData = nullptr;
};

struct Unit
{
    Behaviour behaviour;
    Record record;

    AudioComponentInstance instance = nullptr;
    bool initialized = false;
    bool hostCallbacks = false;
    bool rendered = false;
    UInt32 maxFrames = 1156;
    Float64 sampleRate = 44100.0;
    int inputChannels = 2;
    int outputChannels = 2;
    Float64 latencySeconds = 0.0;
    std::array<AudioUnitParameterValue, numParameters> values {0.5f, 50.f, 0.f};
    AURenderCallbackStruct renderCallback {};
    std::vector<PropertyListener> listeners;

    // Inside its own notification: a read then is a re-entrant one.
    int notifying = 0;

    // The cached name, and its retain count before the host first read it.
    CFStringRef cachedName = nullptr;
    CFIndex cachedNameRetains = 0;

    // Sized on Initialize: what a pull hands the host.
    std::vector<float> pullStorage;
    std::vector<std::uint64_t> pullList;
};

struct Instance
{
    AudioComponentPlugInInterface plugIn {};
    Unit* unit = nullptr;
};

inline Behaviour nextBehaviour;
inline Unit* lastOpened = nullptr;

inline Unit& self(void* ref)
{
    return *static_cast<Instance*>(ref)->unit;
}

// Every listener for the property, by index rather than over a copy, so that
// telling from Render allocates nothing.
inline void
    notify(Unit& unit, AudioUnitPropertyID property, AudioUnitElement element = 0)
{
    ++unit.notifying;

    for (auto i = size_t {0}; i < unit.listeners.size(); ++i)
    {
        auto listener = unit.listeners[i];

        if (listener.property == property)
            listener.proc(listener.userData,
                          unit.instance,
                          property,
                          kAudioUnitScope_Global,
                          element);
    }

    --unit.notifying;
}

// A host read from inside the unit's own notification, which a unit holding its
// own lock there would deadlock on: recorded, and turned down.
inline bool readWhileNotifying(Unit& unit)
{
    if (unit.notifying == 0)
        return false;

    ++unit.record.reentrantReads;
    return true;
}

inline bool listsNothingYet(const Unit& unit)
{
    return unit.behaviour.listOnlyOnceInitialized && !unit.initialized;
}

// Info read from a unit that answers only once initialized: recorded, and
// turned down unless it was set up to answer anyway.
inline bool infoReadTooEarly(Unit& unit)
{
    if (!listsNothingYet(unit))
        return false;

    ++unit.record.uninitializedInfoReads;
    return !unit.behaviour.answerInfoWhileUninitialized;
}

inline AudioStreamBasicDescription formatOf(const Unit& unit, int channels)
{
    auto format = AudioStreamBasicDescription();
    format.mSampleRate = unit.sampleRate;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags =
        static_cast<AudioFormatFlags>(kAudioFormatFlagsNativeFloatPacked)
        | static_cast<AudioFormatFlags>(kAudioFormatFlagIsNonInterleaved);
    format.mFramesPerPacket = 1;
    format.mChannelsPerFrame = static_cast<UInt32>(channels);
    format.mBitsPerChannel = 32;
    format.mBytesPerFrame = sizeof(Float32);
    format.mBytesPerPacket = sizeof(Float32);
    return format;
}

inline const char* nameOf(AudioUnitParameterID id)
{
    static constexpr const char* names[] = {"Gain", "Mix", "Mode"};
    return names[id];
}

inline void
    fillInfo(Unit& unit, AudioUnitParameterID id, AudioUnitParameterInfo& info)
{
    info = AudioUnitParameterInfo();
    info.flags =
        kAudioUnitParameterFlag_IsReadable | kAudioUnitParameterFlag_IsWritable;

    switch (id)
    {
        case 0:
            info.unit = kAudioUnitParameterUnit_Generic;
            info.maxValue = 1.f;
            info.defaultValue = 0.5f;
            break;
        case 1:
            info.unit = kAudioUnitParameterUnit_Percent;
            info.maxValue = 100.f;
            info.defaultValue = 50.f;
            break;
        default:
            info.unit = kAudioUnitParameterUnit_Indexed;
            info.maxValue = 2.f;
            info.flags |= kAudioUnitParameterFlag_ValuesHaveStrings;
            break;
    }

    // As ausdk's FillInParameterName: the C string and a +1 CFString.
    std::strncpy(info.name, nameOf(id), sizeof(info.name) - 1);
    info.cfNameString = CFStringCreateWithCString(
        kCFAllocatorDefault, nameOf(id), kCFStringEncodingUTF8);
    info.flags |= kAudioUnitParameterFlag_HasCFNameString
                  | kAudioUnitParameterFlag_CFNameRelease;

    if (id == 0 && unit.behaviour.cachedNameWithoutFlag)
    {
        CFRelease(info.cfNameString);
        info.cfNameString = unit.cachedName;
        info.flags &= ~static_cast<UInt32>(kAudioUnitParameterFlag_HasCFNameString);
    }

    if (id == 1 && unit.behaviour.unterminatedName)
    {
        CFRelease(info.cfNameString);

        // Every byte non-zero up to the unit field: the name, the padding after
        // it, and pointers no flag says are valid, so a host never touches them.
        auto unitField = offsetof(AudioUnitParameterInfo, unit);
        std::memset(&info, 'x', unitField);
        info.unit = kAudioUnitParameterUnit_Percent;
        info.minValue = 0.f;
        info.maxValue = 100.f;
        info.defaultValue = 50.f;
        info.flags =
            kAudioUnitParameterFlag_IsReadable | kAudioUnitParameterFlag_IsWritable;
    }
}

inline CFArrayRef valueStrings()
{
    CFStringRef strings[] = {CFSTR("Off"), CFSTR("On"), CFSTR("Auto")};
    return CFArrayCreate(kCFAllocatorDefault,
                         reinterpret_cast<const void**>(strings),
                         3,
                         &kCFTypeArrayCallBacks);
}

inline OSStatus open(void* ref, AudioComponentInstance instance)
{
    auto& unit = self(ref);
    unit.instance = instance;
    lastOpened = &unit;

    if (unit.behaviour.cachedNameWithoutFlag)
    {
        // Long enough never to be a tagged pointer, whose count means nothing.
        // Never released: a host that over-releases it must not free it under
        // the test, which reads its count after the unit is gone.
        unit.cachedName = CFStringCreateWithCString(
            kCFAllocatorDefault,
            "A name the unit caches and never hands over to the host",
            kCFStringEncodingUTF8);

        for (auto i = 0; i < 16; ++i)
            CFRetain(unit.cachedName);

        unit.cachedNameRetains = CFGetRetainCount(unit.cachedName);
    }

    return noErr;
}

inline OSStatus close(void* ref)
{
    auto* instance = static_cast<Instance*>(ref);

    if (lastOpened == instance->unit)
        lastOpened = nullptr;

    delete instance->unit;
    delete instance;
    return noErr;
}

inline OSStatus initialize(void* ref)
{
    auto& unit = self(ref);
    ++unit.record.initializeCalls;

    if (!unit.hostCallbacks)
        ++unit.record.initializesWithoutHostCallbacks;

    unit.initialized = true;
    unit.rendered = false;

    auto channels = static_cast<size_t>(unit.inputChannels);
    unit.pullStorage.assign(channels * unit.maxFrames, 0.f);
    auto bytes = sizeof(AudioBufferList) + channels * sizeof(AudioBuffer);
    unit.pullList.assign(bytes / sizeof(std::uint64_t) + 1, 0u);

    if (unit.behaviour.latencyOnInitialize >= 0.0)
    {
        unit.latencySeconds = unit.behaviour.latencyOnInitialize;
        notify(unit, kAudioUnitProperty_Latency);
        notify(unit, kAudioUnitProperty_TailTime);
    }

    return noErr;
}

inline OSStatus uninitialize(void* ref)
{
    self(ref).initialized = false;
    return noErr;
}

inline OSStatus getPropertyInfo(void* ref,
                                AudioUnitPropertyID property,
                                AudioUnitScope scope,
                                AudioUnitElement,
                                UInt32* size,
                                Boolean* writable)
{
    auto& unit = self(ref);

    if (readWhileNotifying(unit))
        return kAudioUnitErr_CannotDoInCurrentContext;

    auto bytes = UInt32 {0};
    auto canWrite = false;

    switch (property)
    {
        case kAudioUnitProperty_ParameterList:
            if (listsNothingYet(unit))
                return kAudioUnitErr_Uninitialized;

            bytes = static_cast<UInt32>(unit.behaviour.ids.size()
                                        * sizeof(AudioUnitParameterID));
            break;
        case kAudioUnitProperty_ParameterInfo:
            bytes = sizeof(AudioUnitParameterInfo);
            break;
        case kAudioUnitProperty_ParameterValueStrings:
            bytes = sizeof(CFArrayRef);
            break;
        case kAudioUnitProperty_Latency:
        case kAudioUnitProperty_TailTime:
            bytes = sizeof(Float64);
            break;
        case kAudioUnitProperty_ElementCount:
            bytes = sizeof(UInt32);
            break;
        case kAudioUnitProperty_StreamFormat:
            bytes = sizeof(AudioStreamBasicDescription);
            canWrite = true;
            break;
        case kAudioUnitProperty_MaximumFramesPerSlice:
            bytes = sizeof(UInt32);
            canWrite = true;
            break;
        case kAudioUnitProperty_SupportedNumChannels:
            if (!unit.behaviour.anyChannelCount || scope != kAudioUnitScope_Global)
                return kAudioUnitErr_InvalidProperty;

            bytes = sizeof(AUChannelInfo);
            break;
        case kAudioUnitProperty_SetRenderCallback:
            bytes = sizeof(AURenderCallbackStruct);
            canWrite = true;
            break;
        case kAudioUnitProperty_HostCallbacks:
            bytes = sizeof(HostCallbackInfo);
            canWrite = true;
            break;
        default:
            return kAudioUnitErr_InvalidProperty;
    }

    if (size != nullptr)
        *size = bytes;

    if (writable != nullptr)
        *writable = canWrite;

    return noErr;
}

inline OSStatus getProperty(void* ref,
                            AudioUnitPropertyID property,
                            AudioUnitScope scope,
                            AudioUnitElement element,
                            void* data,
                            UInt32* size)
{
    auto& unit = self(ref);

    if (readWhileNotifying(unit))
        return kAudioUnitErr_CannotDoInCurrentContext;

    if (data == nullptr || size == nullptr)
        return kAudio_ParamError;

    switch (property)
    {
        case kAudioUnitProperty_ParameterList:
        {
            if (listsNothingYet(unit))
                return kAudioUnitErr_Uninitialized;

            const auto& ids = unit.behaviour.ids;
            auto count =
                std::min<size_t>(ids.size(), *size / sizeof(AudioUnitParameterID));
            std::copy_n(
                ids.begin(), count, static_cast<AudioUnitParameterID*>(data));
            *size = static_cast<UInt32>(count * sizeof(AudioUnitParameterID));
            return noErr;
        }

        case kAudioUnitProperty_ParameterInfo:
        {
            if (element >= static_cast<UInt32>(numParameters))
                return kAudioUnitErr_InvalidParameter;

            if (infoReadTooEarly(unit))
                return kAudioUnitErr_Uninitialized;

            fillInfo(unit, element, *static_cast<AudioUnitParameterInfo*>(data));
            *size = sizeof(AudioUnitParameterInfo);
            return noErr;
        }

        case kAudioUnitProperty_ParameterValueStrings:
        {
            if (element != 2)
                return kAudioUnitErr_InvalidProperty;

            if (infoReadTooEarly(unit))
                return kAudioUnitErr_Uninitialized;

            *static_cast<CFArrayRef*>(data) = valueStrings();
            *size = sizeof(CFArrayRef);
            return noErr;
        }

        case kAudioUnitProperty_Latency:
            *static_cast<Float64*>(data) = unit.latencySeconds;
            *size = sizeof(Float64);
            return noErr;

        case kAudioUnitProperty_TailTime:
            *static_cast<Float64*>(data) = 0.0;
            *size = sizeof(Float64);
            return noErr;

        case kAudioUnitProperty_ElementCount:
            *static_cast<UInt32*>(data) = 1;
            *size = sizeof(UInt32);
            return noErr;

        case kAudioUnitProperty_StreamFormat:
        {
            if (element != 0 || scope == kAudioUnitScope_Global)
                return kAudioUnitErr_InvalidElement;

            auto channels = scope == kAudioUnitScope_Input ? unit.inputChannels
                                                           : unit.outputChannels;
            *static_cast<AudioStreamBasicDescription*>(data) =
                formatOf(unit, channels);
            *size = sizeof(AudioStreamBasicDescription);
            return noErr;
        }

        case kAudioUnitProperty_MaximumFramesPerSlice:
            *static_cast<UInt32*>(data) = unit.maxFrames;
            *size = sizeof(UInt32);
            return noErr;

        case kAudioUnitProperty_SupportedNumChannels:
            if (!unit.behaviour.anyChannelCount)
                return kAudioUnitErr_InvalidProperty;

            *static_cast<AUChannelInfo*>(data) = AUChannelInfo {-1, -1};
            *size = sizeof(AUChannelInfo);
            return noErr;

        default:
            return kAudioUnitErr_InvalidProperty;
    }
}

inline OSStatus setProperty(void* ref,
                            AudioUnitPropertyID property,
                            AudioUnitScope scope,
                            AudioUnitElement element,
                            const void* data,
                            UInt32 size)
{
    auto& unit = self(ref);

    switch (property)
    {
        case kAudioUnitProperty_MaximumFramesPerSlice:
            if (data == nullptr || size < sizeof(UInt32) || unit.initialized)
                return kAudio_ParamError;

            unit.maxFrames = *static_cast<const UInt32*>(data);
            return noErr;

        case kAudioUnitProperty_StreamFormat:
        {
            if (data == nullptr || size < sizeof(AudioStreamBasicDescription)
                || element != 0 || scope == kAudioUnitScope_Global)
                return kAudio_ParamError;

            const auto& format =
                *static_cast<const AudioStreamBasicDescription*>(data);
            auto channels = static_cast<int>(format.mChannelsPerFrame);
            auto only = unit.behaviour.onlyOutputChannels;

            if (format.mFormatID != kAudioFormatLinearPCM || channels <= 0
                || (scope == kAudioUnitScope_Output && only > 0 && channels != only))
                return kAudioUnitErr_FormatNotSupported;

            unit.sampleRate = format.mSampleRate;
            (scope == kAudioUnitScope_Input ? unit.inputChannels
                                            : unit.outputChannels) = channels;
            return noErr;
        }

        case kAudioUnitProperty_SetRenderCallback:
            if (data == nullptr || size < sizeof(AURenderCallbackStruct)
                || scope != kAudioUnitScope_Input || element != 0)
                return kAudio_ParamError;

            unit.renderCallback = *static_cast<const AURenderCallbackStruct*>(data);
            return noErr;

        case kAudioUnitProperty_HostCallbacks:
            unit.hostCallbacks = data != nullptr && size > 0;
            return noErr;

        default:
            return kAudioUnitErr_InvalidProperty;
    }
}

inline OSStatus getParameter(void* ref,
                             AudioUnitParameterID id,
                             AudioUnitScope,
                             AudioUnitElement,
                             AudioUnitParameterValue* value)
{
    if (id >= static_cast<UInt32>(numParameters) || value == nullptr)
        return kAudioUnitErr_InvalidParameter;

    *value = self(ref).values[id];
    return noErr;
}

inline OSStatus setParameter(void* ref,
                             AudioUnitParameterID id,
                             AudioUnitScope,
                             AudioUnitElement,
                             AudioUnitParameterValue value,
                             UInt32)
{
    if (id >= static_cast<UInt32>(numParameters))
        return kAudioUnitErr_InvalidParameter;

    self(ref).values[id] = value;
    return noErr;
}

inline OSStatus addPropertyListener(void* ref,
                                    AudioUnitPropertyID property,
                                    AudioUnitPropertyListenerProc proc,
                                    void* userData)
{
    self(ref).listeners.push_back({property, proc, userData});
    return noErr;
}

inline OSStatus removePropertyListener(void* ref,
                                       AudioUnitPropertyID property,
                                       AudioUnitPropertyListenerProc proc,
                                       void* userData)
{
    auto& listeners = self(ref).listeners;
    std::erase_if(listeners,
                  [&](const PropertyListener& listener)
                  {
                      return listener.property == property && listener.proc == proc
                             && listener.userData == userData;
                  });
    return noErr;
}

// The output list matches the format the unit holds: one buffer a channel, each
// big enough for the block.
inline bool matchesFormat(const AudioBufferList& list, int channels, UInt32 frames)
{
    if (list.mNumberBuffers != static_cast<UInt32>(channels))
        return false;

    for (auto i = UInt32 {0}; i < list.mNumberBuffers; ++i)
    {
        const auto& buffer = list.mBuffers[i];

        if (buffer.mNumberChannels != 1 || buffer.mDataByteSize < frames * 4)
            return false;
    }

    return true;
}

inline void pullInput(Unit& unit, const AudioTimeStamp* time, UInt32 frames)
{
    if (unit.renderCallback.inputProc == nullptr)
        return;

    const auto& behaviour = unit.behaviour;
    auto pulled = behaviour.pullMaxFrames ? unit.maxFrames : frames;
    auto* list = reinterpret_cast<AudioBufferList*>(unit.pullList.data());
    list->mNumberBuffers = static_cast<UInt32>(unit.inputChannels);

    for (auto ch = 0; ch < unit.inputChannels; ++ch)
    {
        auto* own =
            unit.pullStorage.data() + static_cast<size_t>(ch) * unit.maxFrames;
        std::fill_n(own, pulled, pullSentinel);

        auto& buffer = list->mBuffers[ch];
        buffer.mNumberChannels = 1;
        buffer.mDataByteSize = pulled * sizeof(float);
        buffer.mData = behaviour.pullIntoHostMemory ? nullptr : own;
    }

    auto flags = AudioUnitRenderActionFlags {0};
    auto status = unit.renderCallback.inputProc(
        unit.renderCallback.inputProcRefCon, &flags, time, 0, pulled, list);
    ++unit.record.pulls;

    if (status != noErr)
        return;

    for (auto ch = UInt32 {0}; ch < list->mNumberBuffers; ++ch)
    {
        auto* received = static_cast<float*>(list->mBuffers[ch].mData);
        auto available = list->mBuffers[ch].mDataByteSize / sizeof(float);

        if (received == nullptr)
            continue;

        if (behaviour.pullIntoHostMemory)
        {
            auto inBlock = std::min<size_t>(frames, available);

            for (auto i = size_t {0}; i < inBlock; ++i)
                if (received[i] != 0.f)
                    ++unit.record.nonSilentSamples;

            // Processing in place, in the memory the host handed over.
            std::fill_n(received, inBlock, 0.5f);
            continue;
        }

        // Past the block, a host supplies nothing or zeros: anything else came
        // from beyond the input it was given.
        for (auto i = size_t {frames}; i < std::min<size_t>(pulled, available); ++i)
            if (received[i] != pullSentinel && received[i] != 0.f)
                ++unit.record.samplesFromBeyondTheBlock;
    }
}

inline OSStatus render(void* ref,
                       AudioUnitRenderActionFlags*,
                       const AudioTimeStamp* time,
                       UInt32 bus,
                       UInt32 frames,
                       AudioBufferList* io)
{
    auto& unit = self(ref);

    if (!unit.initialized)
        return kAudioUnitErr_Uninitialized;

    if (bus != 0 || io == nullptr)
        return kAudioUnitErr_InvalidElement;

    ++unit.record.renders;

    if (!matchesFormat(*io, unit.outputChannels, frames))
    {
        ++unit.record.inconsistentRenders;
        return kAudioUnitErr_FormatNotSupported;
    }

    if (frames > unit.maxFrames)
        return kAudioUnitErr_TooManyFramesToProcess;

    pullInput(unit, time, frames);

    for (auto i = UInt32 {0}; i < io->mNumberBuffers; ++i)
        if (auto* out = static_cast<float*>(io->mBuffers[i].mData))
            std::fill_n(out, frames, 0.f);

    const auto& behaviour = unit.behaviour;

    if (behaviour.latencyOnFirstRender >= 0.0
        && (!unit.rendered || behaviour.tellLatencyOnEveryRender))
    {
        unit.latencySeconds = behaviour.latencyOnFirstRender;
        notify(unit, kAudioUnitProperty_Latency);
        notify(unit, kAudioUnitProperty_TailTime);
    }

    unit.rendered = true;
    return noErr;
}

inline OSStatus reset(void*, AudioUnitScope, AudioUnitElement)
{
    return noErr;
}

inline AudioComponentMethod lookup(SInt16 selector)
{
    auto method = [](auto function)
    { return reinterpret_cast<AudioComponentMethod>(function); };

    switch (selector)
    {
        case kAudioUnitInitializeSelect:
            return method(&initialize);
        case kAudioUnitUninitializeSelect:
            return method(&uninitialize);
        case kAudioUnitGetPropertyInfoSelect:
            return method(&getPropertyInfo);
        case kAudioUnitGetPropertySelect:
            return method(&getProperty);
        case kAudioUnitSetPropertySelect:
            return method(&setProperty);
        case kAudioUnitGetParameterSelect:
            return method(&getParameter);
        case kAudioUnitSetParameterSelect:
            return method(&setParameter);
        case kAudioUnitAddPropertyListenerSelect:
            return method(&addPropertyListener);
        case kAudioUnitRemovePropertyListenerWithUserDataSelect:
            return method(&removePropertyListener);
        case kAudioUnitRenderSelect:
            return method(&render);
        case kAudioUnitResetSelect:
            return method(&reset);
        default:
            return nullptr;
    }
}

inline AudioComponentPlugInInterface* factory(const AudioComponentDescription*)
{
    auto* instance = new Instance();
    instance->unit = new Unit();
    instance->unit->behaviour = nextBehaviour;
    instance->plugIn.Open = &open;
    instance->plugIn.Close = &close;
    instance->plugIn.Lookup = &lookup;
    return &instance->plugIn;
}

inline AudioComponent component()
{
    static const auto registered = []
    {
        auto description = AudioComponentDescription {
            .componentType = kAudioUnitType_Effect,
            .componentSubType = FourCC("HsRb").toUint32(),
            .componentManufacturer = FourCC("MkTs").toUint32()};

        return AudioComponentRegister(
            &description, CFSTR("MakeASound Tests: Misbehaving Unit"), 1, &factory);
    }();

    nano::check(registered != nullptr, "the misbehaving unit registers");
    return registered;
}

struct Loaded
{
    OwningPointer<HostedAudioUnit> plugin;
    Unit* unit = nullptr;

    explicit operator bool() const { return plugin != nullptr && unit != nullptr; }
};

inline Loaded load(const Behaviour& behaviour = {})
{
    auto loaded = Loaded();
    auto* registered = component();

    if (registered == nullptr)
        return loaded;

    nextBehaviour = behaviour;
    lastOpened = nullptr;

    auto description = Hosting::PluginDescription();
    description.name = "Misbehaving Unit";

    auto result = loadAudioUnit(registered, description, {});
    nextBehaviour = Behaviour();
    nano::check(result.error.empty(), "the misbehaving unit loads");

    auto* unit = dynamic_cast<HostedAudioUnit*>(result.plugin.get());
    nano::check(unit != nullptr && lastOpened != nullptr,
                "the load is a HostedAudioUnit");

    if (unit == nullptr || lastOpened == nullptr)
        return loaded;

    result.plugin.release();
    loaded.plugin = OwningPointer<HostedAudioUnit>(unit);
    loaded.unit = lastOpened;
    return loaded;
}
} // namespace MisbehavingAU
