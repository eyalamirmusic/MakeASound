#pragma once

// The AU adapter hosted in-process through the C API alone, the way a DAW drives
// it, shared by the AU suites: the test module's plugins registered with our
// factory, and a fixture owning the unit, the host's buffers, the pull-input
// callback, the timestamp and the MIDI the unit sends back. No adapter header:
// the SDK is C++23 and these suites are not.

#include "TestPlugins.h"

#include <MakeASound/Plugin/AU/ComponentType.h>

#include <NanoTest/NanoTest.h>

#include <AudioToolbox/AudioToolbox.h>
#include <CoreMIDI/CoreMIDI.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <map>
#include <tuple>
#include <vector>

extern "C" void* MakeASoundAUFactory(const AudioComponentDescription* desc);

namespace AUHost
{
using namespace MakeASound;

constexpr auto maxBlock = TestPlugins::blockSize;
constexpr auto sampleRate = 48000.0;

// What a host buffer can hold, past any block a case renders.
constexpr auto bufferCapacity = 1024;

inline AudioComponentPlugInInterface* factory(const AudioComponentDescription* desc)
{
    return static_cast<AudioComponentPlugInInterface*>(MakeASoundAUFactory(desc));
}

// In this process only, and once per description: instantiating from the handle
// this returns never reaches an installed bundle, as AudioComponentFindNext could.
inline AudioComponent registerComponent(const AudioComponentDescription& desc)
{
    static auto registered =
        std::map<std::tuple<OSType, OSType, OSType>, AudioComponent> {};

    auto key = std::make_tuple(
        desc.componentType, desc.componentSubType, desc.componentManufacturer);

    if (auto found = registered.find(key); found != registered.end())
        return found->second;

    auto* component =
        AudioComponentRegister(&desc, CFSTR("MakeASound Tests"), 0x10000, &factory);
    registered.emplace(key, component);
    return component;
}

inline const PluginDescription& pluginFor(FourCC code)
{
    static const auto module = describeModule();

    for (const auto& plugin: module.plugins)
        if (plugin.pluginCode == code)
            return plugin;

    return module.plugins[0];
}

inline AudioComponentDescription descriptionFor(FourCC code)
{
    static const auto module = describeModule();
    auto info = AU::componentInfoFor(module, pluginFor(code));

    return {.componentType = info.type,
            .componentSubType = info.subtype,
            .componentManufacturer = info.manufacturer};
}

inline void registerTestComponents()
{
    static const auto once = []
    {
        for (const auto& plugin: describeModule().plugins)
            registerComponent(descriptionFor(plugin.pluginCode));

        return true;
    }();

    std::ignore = once;
}

inline AudioComponent componentFor(FourCC code)
{
    registerTestComponents();
    return registerComponent(descriptionFor(code));
}

// A packet the unit sent, kept in fixed storage so collecting it never allocates.
struct SentMidi
{
    UInt32 bus = 0;
    MIDITimeStamp offset = 0;
    UInt16 length = 0;
    std::array<Byte, 64> bytes {};

    bool is(std::initializer_list<int> expected) const
    {
        return std::equal(
            bytes.begin(), bytes.begin() + length, expected.begin(), expected.end());
    }
};

// One bus of host memory and the AudioBufferList a host hands over.
struct HostBus
{
    explicit HostBus(int numChannels)
        : storage(static_cast<size_t>(numChannels),
                  std::vector<float>(bufferCapacity))
        , listStorage(offsetof(AudioBufferList, mBuffers)
                      + std::max(1, numChannels) * sizeof(AudioBuffer))
    {
    }

    int numChannels() const { return static_cast<int>(storage.size()); }

    void fill(float value)
    {
        for (auto& channel: storage)
            std::fill(channel.begin(), channel.end(), value);
    }

    // Null data asks the unit for its own buffers, which it hands back.
    AudioBufferList& list(UInt32 frames, bool hostMemory = true)
    {
        auto& result = *reinterpret_cast<AudioBufferList*>(listStorage.data());
        result.mNumberBuffers = static_cast<UInt32>(storage.size());

        for (auto ch = 0; ch < numChannels(); ++ch)
            result.mBuffers[ch] = {
                1,
                static_cast<UInt32>(frames * sizeof(float)),
                hostMemory ? storage[static_cast<size_t>(ch)].data() : nullptr};

        return result;
    }

    std::vector<std::vector<float>> storage;
    std::vector<std::byte> listStorage;
};

enum class Start
{
    Initialized,
    Open
};

// A unit of the test module, opened and, unless asked otherwise, initialized at
// 48 kHz in slices of up to 64 with every input fed by the pull callback and the
// MIDI-out callback installed.
template <typename P>
struct UnitHost
{
    explicit UnitHost(FourCC code, Start start = Start::Initialized)
    {
        TestPlugins::Module::lastCreated = nullptr;
        status = AudioComponentInstanceNew(componentFor(code), &unit);
        nano::check(status == noErr && unit != nullptr);
        pluginPtr = static_cast<P*>(TestPlugins::Module::lastCreated);
        sent.reserve(256);

        if (start == Start::Initialized)
            nano::check(initialize() == noErr);
    }

    ~UnitHost()
    {
        if (unit != nullptr)
        {
            AudioUnitUninitialize(unit);
            AudioComponentInstanceDispose(unit);
        }
    }

    UnitHost(const UnitHost&) = delete;
    UnitHost& operator=(const UnitHost&) = delete;

    P& plugin() { return *pluginPtr; }

    UInt32 elementCount(AudioUnitScope scope) const
    {
        auto count = UInt32 {};
        auto size = UInt32 {sizeof(count)};
        AudioUnitGetProperty(
            unit, kAudioUnitProperty_ElementCount, scope, 0, &count, &size);
        return count;
    }

    AudioStreamBasicDescription format(AudioUnitScope scope, UInt32 element) const
    {
        auto result = AudioStreamBasicDescription {};
        auto size = UInt32 {sizeof(result)};
        AudioUnitGetProperty(
            unit, kAudioUnitProperty_StreamFormat, scope, element, &result, &size);
        return result;
    }

    OSStatus setFormat(AudioUnitScope scope,
                       UInt32 element,
                       const AudioStreamBasicDescription& value)
    {
        return AudioUnitSetProperty(unit,
                                    kAudioUnitProperty_StreamFormat,
                                    scope,
                                    element,
                                    &value,
                                    sizeof(value));
    }

    OSStatus setChannels(AudioUnitScope scope, UInt32 element, int channels)
    {
        auto value = format(scope, element);
        value.mChannelsPerFrame = static_cast<UInt32>(channels);
        return setFormat(scope, element, value);
    }

    bool hasMidiOut() const
    {
        auto size = UInt32 {};
        auto writable = Boolean {};
        return AudioUnitGetPropertyInfo(unit,
                                        kAudioUnitProperty_MIDIOutputCallback,
                                        kAudioUnitScope_Global,
                                        0,
                                        &size,
                                        &writable)
               == noErr;
    }

    OSStatus initialize(double rate = sampleRate, UInt32 maxFrames = maxBlock)
    {
        AudioUnitUninitialize(unit);

        AudioUnitSetProperty(unit,
                             kAudioUnitProperty_MaximumFramesPerSlice,
                             kAudioUnitScope_Global,
                             0,
                             &maxFrames,
                             sizeof(maxFrames));

        for (auto scope: {kAudioUnitScope_Input, kAudioUnitScope_Output})
        {
            for (auto element = UInt32 {0}; element < elementCount(scope); ++element)
            {
                auto value = format(scope, element);
                value.mSampleRate = rate;
                setFormat(scope, element, value);
            }
        }

        for (auto element = UInt32 {0};
             connectInputs && element < elementCount(kAudioUnitScope_Input);
             ++element)
        {
            auto callback = AURenderCallbackStruct {&pullInput, this};
            AudioUnitSetProperty(unit,
                                 kAudioUnitProperty_SetRenderCallback,
                                 kAudioUnitScope_Input,
                                 element,
                                 &callback,
                                 sizeof(callback));
        }

        if (collectMidi && hasMidiOut())
        {
            auto callback = AUMIDIOutputCallbackStruct {&receiveMidi, this};
            AudioUnitSetProperty(unit,
                                 kAudioUnitProperty_MIDIOutputCallback,
                                 kAudioUnitScope_Global,
                                 0,
                                 &callback,
                                 sizeof(callback));
        }

        auto result = AudioUnitInitialize(unit);

        inputs.clear();
        outputs.clear();

        for (auto element = UInt32 {0};
             element < elementCount(kAudioUnitScope_Input);
             ++element)
            inputs.emplace_back(static_cast<int>(
                format(kAudioUnitScope_Input, element).mChannelsPerFrame));

        for (auto element = UInt32 {0};
             element < elementCount(kAudioUnitScope_Output);
             ++element)
            outputs.emplace_back(static_cast<int>(
                format(kAudioUnitScope_Output, element).mChannelsPerFrame));

        return result;
    }

    // Renders output bus `bus` into the host's memory, or into the unit's own
    // buffers when `hostMemory` is false, which `rendered` then points at.
    OSStatus render(UInt32 frames, bool hostMemory = true, UInt32 bus = 0)
    {
        auto flags = AudioUnitRenderActionFlags {};
        timestamp = {};
        timestamp.mSampleTime = sampleTime;
        timestamp.mFlags = kAudioTimeStampSampleTimeValid;

        rendered = &outputs[bus].list(frames, hostMemory);
        auto result =
            AudioUnitRender(unit, &flags, &timestamp, bus, frames, rendered);
        sampleTime += frames;
        return result;
    }

    float renderedSample(int channel, int sample) const
    {
        return static_cast<const float*>(rendered->mBuffers[channel].mData)[sample];
    }

    AudioUnitParameterID hostIdOf(const Parameter& param)
    {
        const auto& list = plugin().parameters();
        return list.entry(list.indexOf(&param)).hostId;
    }

    float hostValue(AudioUnitParameterID id) const
    {
        auto value = AudioUnitParameterValue {};
        nano::check(
            AudioUnitGetParameter(unit, id, kAudioUnitScope_Global, 0, &value)
            == noErr);
        return value;
    }

    OSStatus hostWrites(AudioUnitParameterID id, float value) const
    {
        return AudioUnitSetParameter(unit, id, kAudioUnitScope_Global, 0, value, 0);
    }

    static OSStatus pullInput(void* refCon,
                              AudioUnitRenderActionFlags*,
                              const AudioTimeStamp*,
                              UInt32 bus,
                              UInt32 frames,
                              AudioBufferList* ioData)
    {
        auto& host = *static_cast<UnitHost*>(refCon);
        auto& source = host.inputs[bus].storage;
        auto bytes = static_cast<UInt32>(frames * sizeof(float));
        ++host.pulls;

        for (auto ch = UInt32 {0}; ch < ioData->mNumberBuffers; ++ch)
        {
            auto& buffer = ioData->mBuffers[ch];
            const auto* from = source[ch % source.size()].data();

            // In place: the callback hands back the host's own output memory,
            // which a render callback may, so input and output alias.
            if (host.inPlace && bus < host.outputs.size())
            {
                auto* into = host.outputs[bus].storage[ch].data();
                std::memcpy(into, from, bytes);
                buffer.mData = into;
            }
            else if (buffer.mData == nullptr)
            {
                buffer.mData = const_cast<float*>(from);
            }
            else
            {
                std::memcpy(buffer.mData, from, bytes);
            }

            buffer.mDataByteSize = bytes;
        }

        return noErr;
    }

    static OSStatus receiveMidi(void* userData,
                                const AudioTimeStamp*,
                                UInt32 bus,
                                const MIDIPacketList* list)
    {
        auto& host = *static_cast<UnitHost*>(userData);
        const auto* packet = &list->packet[0];

        for (auto i = UInt32 {0}; i < list->numPackets; ++i)
        {
            if (host.sent.size() < host.sent.capacity())
            {
                auto entry = SentMidi {.bus = bus, .offset = packet->timeStamp};
                entry.length = std::min<UInt16>(packet->length, entry.bytes.size());
                std::copy_n(packet->data, entry.length, entry.bytes.begin());
                host.sent.push_back(entry);
            }

            packet = MIDIPacketNext(packet);
        }

        ++host.midiCallbacks;
        return noErr;
    }

    AudioComponentInstance unit = nullptr;
    OSStatus status = noErr;
    P* pluginPtr = nullptr;

    bool connectInputs = true;
    bool collectMidi = true;
    bool inPlace = false;

    std::vector<HostBus> inputs;
    std::vector<HostBus> outputs;
    AudioBufferList* rendered = nullptr;

    AudioTimeStamp timestamp {};
    Float64 sampleTime = 0.0;
    int pulls = 0;

    std::vector<SentMidi> sent;
    int midiCallbacks = 0;
};

} // namespace AUHost
