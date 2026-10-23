#pragma once

// The AU adapter hosted in-process through the C API alone, the way a DAW drives
// it, shared by the AU suites: the test module's plugins registered with our
// factory, and a fixture owning the unit, the host's buffers, the pull-input
// callback, the timestamp and the MIDI the unit sends back. No adapter header:
// the SDK is C++23 and these suites are not.

#include "HostBuffers.h"
#include "JucePort.h"
#include "TamberPlugFixtures.h"
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
extern "C" void* MakeASoundAUTestPortFactory(const AudioComponentDescription* desc);

// The plugin behind a unit of this binary's adapter, asked of the adapter itself
// (AUTestPluginAccess.cpp, built with the module's entry); null for any other.
extern "C" MakeASound::Plugin* MakeASoundAUTestPluginOf(AudioUnit unit);

namespace AUHost
{
using namespace MakeASound;

constexpr auto maxBlock = TestPlugins::blockSize;
constexpr auto sampleRate = 48000.0;

inline Plugin* pluginOf(AudioUnit unit)
{
    return unit != nullptr ? MakeASoundAUTestPluginOf(unit) : nullptr;
}

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

// JucePort.h's descriptions, which are no part of describeModule(): the test
// module's MakeASoundAUTestPortFactory builds the module's adapter over them.
inline AudioComponentPlugInInterface*
    portFactory(const AudioComponentDescription* desc)
{
    return static_cast<AudioComponentPlugInInterface*>(
        MakeASoundAUTestPortFactory(desc));
}

inline const ModuleDescription& portModule()
{
    static const auto module = []
    {
        auto result = ModuleDescription {};
        result.vendor = "MakeASound Tests";
        result.manufacturerCode = JucePort::manufacturer;
        result.plugins.add(JucePort::description(IdScheme::JUCE));
        result.plugins.add(JucePort::description(IdScheme::JUCELegacyIndex));
        return result;
    }();

    return module;
}

inline AudioComponentDescription portDescriptionFor(IdScheme scheme)
{
    auto info = AU::componentInfoFor(portModule(), JucePort::description(scheme));

    return {.componentType = info.type,
            .componentSubType = info.subtype,
            .componentManufacturer = info.manufacturer};
}

// Once per scheme, under the type its plist would register.
inline AudioComponent portComponentFor(IdScheme scheme)
{
    static auto registered = std::map<IdScheme, AudioComponent> {};

    if (auto found = registered.find(scheme); found != registered.end())
        return found->second;

    auto desc = portDescriptionFor(scheme);
    auto* component = AudioComponentRegister(
        &desc, CFSTR("MakeASound Tests: Port"), 0x10000, &portFactory);
    registered.emplace(scheme, component);
    return component;
}

// One of TamberPlugFixtures' ports under IdScheme::TamberPlug, by its code, once.
inline AudioComponent plugPortComponentFor(FourCC code)
{
    static auto registered = std::map<uint32_t, AudioComponent> {};

    if (auto found = registered.find(code.toUint32()); found != registered.end())
        return found->second;

    const auto* plugin = TamberPlugFixtures::plugDescriptionFor(code.toUint32());
    auto info = AU::componentInfoFor(portModule(), *plugin);
    auto desc =
        AudioComponentDescription {.componentType = info.type,
                                   .componentSubType = info.subtype,
                                   .componentManufacturer = info.manufacturer};
    auto* component = AudioComponentRegister(
        &desc, CFSTR("MakeASound Tests: Plug Port"), 0x10000, &portFactory);
    registered.emplace(code.toUint32(), component);
    return component;
}

using HostBuffers::HostBus;
using HostBuffers::SentMidi;

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
        : UnitHost(componentFor(code), start)
    {
    }

    explicit UnitHost(AudioComponent component, Start start = Start::Initialized)
    {
        status = AudioComponentInstanceNew(component, &unit);
        nano::check(status == noErr && unit != nullptr);
        pluginPtr = static_cast<P*>(pluginOf(unit));
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
