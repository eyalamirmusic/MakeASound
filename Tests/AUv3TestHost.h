#pragma once

// Objective-C++ only. The AUv3 adapter hosted in-process the way an app hosts it,
// shared by the AUv3 suites: the test module's AUAudioUnit subclass registered
// with AudioToolbox, and a fixture owning a unit, the host's buffers, the
// pull-input block, the timestamp and the MIDI the unit sends back.

#include "HostBuffers.h"
#include "JucePort.h"
#include "TestPlugins.h"

#include <MakeASound/Plugin/AU/ComponentType.h>
#include <MakeASound/Plugin/AUv3/Adapter.h>
#include <MakeASound/Plugin/AUv3/AudioUnitFactory.h>

#include <NanoTest/NanoTest.h>

#import <AVFoundation/AVFoundation.h>
#import <AudioToolbox/AudioToolbox.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace AUv3Host
{
using namespace MakeASound;
using HostBuffers::HostBus;
using HostBuffers::SentMidi;

constexpr auto maxBlock = TestPlugins::blockSize;
constexpr auto sampleRate = 48000.0;

// kAudioComponentInstantiation_LoadInProcess on macOS; iOS has no such option,
// and a registered subclass loads in process there anyway. One TU per platform.
AudioComponentInstantiationOptions inProcessOption() noexcept;

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

// The description a plugin of the test module's manufacturer registers under,
// its auType honoured.
inline AudioComponentDescription descriptionOf(const PluginDescription& plugin)
{
    static const auto module = describeModule();
    auto info = AU::componentInfoFor(module, plugin);

    return {.componentType = info.type,
            .componentSubType = info.subtype,
            .componentManufacturer = info.manufacturer};
}

inline AudioComponentDescription registerSubclass(AudioComponentDescription desc)
{
    [AUAudioUnit registerSubclass:AUv3::audioUnitClass()
           asComponentDescription:desc
                             name:@"MakeASound Tests: AUv3"
                          version:0x10000];
    return desc;
}

// Once per process, every plugin of the test module, as an extension's plist
// would declare them.
inline void registerTestUnits()
{
    static const auto once = []
    {
        for (const auto& plugin: describeModule().plugins)
            registerSubclass(descriptionFor(plugin.pluginCode));

        return true;
    }();

    std::ignore = once;
}

// The registered subclass, constructed directly: the v2 suite registers the same
// descriptions with its factory, and a lookup by description could find those.
inline AUAudioUnit* instantiate(AudioComponentDescription desc, NSError** error)
{
    registerTestUnits();

    return [[AUv3::audioUnitClass() alloc]
        initWithComponentDescription:desc
                             options:inProcessOption()
                               error:error];
}

inline AVAudioFormat* floatFormat(double rate, int channels)
{
    auto description = AudioStreamBasicDescription {};
    description.mSampleRate = rate;
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

enum class Start
{
    Allocated,
    Open
};

// A unit of the test module, and unless asked otherwise its render resources
// allocated at 48 kHz for slices of up to 64, every input fed by the pull block
// and the MIDI-out block installed.
template <typename P>
struct UnitHost
{
    explicit UnitHost(FourCC code, Start start = Start::Allocated)
        : UnitHost(
              Creating {},
              [&](NSError** error) { return instantiate(descriptionFor(code), error); },
              start)
    {
    }

    // A unit over `plugin` (JucePort.h's), which describeModule() does not list,
    // under the type its plist would register.
    explicit UnitHost(const PluginDescription& plugin, Start start = Start::Allocated)
        : UnitHost(
              Creating {},
              [&](NSError** error)
              {
                  return AUv3::createAudioUnit(
                      descriptionOf(plugin), plugin, inProcessOption(), error);
              },
              start)
    {
    }

    struct Creating
    {
    };

    template <typename Create>
    UnitHost(Creating, Create create, Start start)
    {
        auto* error = static_cast<NSError*>(nil);
        unit = create(&error);
        nano::check(unit != nil && error == nil);

        // Asked of the unit's adapter, which is this module's class's only.
        if (auto* adapter = AUv3::adapterOf(unit))
            pluginPtr = static_cast<P*>(&adapter->plugin());
        sent.reserve(256);

        auto* host = this;

        pullInput = ^AUAudioUnitStatus(AudioUnitRenderActionFlags*,
                                       const AudioTimeStamp*,
                                       AUAudioFrameCount frames,
                                       NSInteger bus,
                                       AudioBufferList* input) {
          return host->pull(frames, static_cast<int>(bus), input);
        };

        midiOut = ^OSStatus(AUEventSampleTime time,
                            uint8_t cable,
                            NSInteger length,
                            const uint8_t* bytes) {
          return host->receiveMidi(time, cable, length, bytes);
        };

        if (start == Start::Allocated)
            nano::check(allocate());
    }

    ~UnitHost()
    {
        if (unit.renderResourcesAllocated)
            [unit deallocateRenderResources];

        unit = nil;
    }

    UnitHost(const UnitHost&) = delete;
    UnitHost& operator=(const UnitHost&) = delete;

    P& plugin() { return *pluginPtr; }

    AUAudioUnitBus* bus(bool input, int index) const
    {
        auto* buses = input ? unit.inputBusses : unit.outputBusses;
        return buses[static_cast<NSUInteger>(index)];
    }

    bool setChannels(bool input, int index, int channels, double rate = sampleRate)
    {
        return [bus(input, index) setFormat:floatFormat(rate, channels) error:nil];
    }

    bool allocate(double rate = sampleRate, AUAudioFrameCount maxFrames = maxBlock)
    {
        if (unit.renderResourcesAllocated)
            [unit deallocateRenderResources];

        unit.maximumFramesToRender = maxFrames;

        for (auto input: {true, false})
        {
            auto* buses = input ? unit.inputBusses : unit.outputBusses;

            for (NSUInteger index = 0; index < buses.count; ++index)
            {
                auto channels = static_cast<int>(buses[index].format.channelCount);
                [buses[index] setFormat:floatFormat(rate, channels) error:nil];
            }
        }

        unit.MIDIOutputEventBlock = nil;

        if (collectMidi)
            unit.MIDIOutputEventBlock = midiOut;

        auto* error = static_cast<NSError*>(nil);
        auto allocated = [unit allocateRenderResourcesAndReturnError:&error];

        inputs.clear();
        outputs.clear();

        for (NSUInteger index = 0; index < unit.inputBusses.count; ++index)
            inputs.emplace_back(
                static_cast<int>(unit.inputBusses[index].format.channelCount));

        for (NSUInteger index = 0; index < unit.outputBusses.count; ++index)
            outputs.emplace_back(
                static_cast<int>(unit.outputBusses[index].format.channelCount));

        internal = unit.internalRenderBlock;
        return allocated;
    }

    AudioTimeStamp nextTimestamp()
    {
        auto result = AudioTimeStamp {};
        result.mSampleTime = sampleTime;
        result.mFlags = kAudioTimeStampSampleTimeValid;
        return result;
    }

    // Renders output bus `bus` through the unit's renderBlock, as a host does,
    // into the host's memory or, when `hostMemory` is false, with null data the
    // unit fills in, which `rendered` then points at.
    AUAudioUnitStatus
        render(AUAudioFrameCount frames, bool hostMemory = true, int bus = 0)
    {
        auto flags = AudioUnitRenderActionFlags {};
        timestamp = nextTimestamp();
        rendered = &outputs[static_cast<size_t>(bus)].list(frames, hostMemory);

        auto result =
            unit.renderBlock(&flags, &timestamp, frames, bus, rendered, pullBlock());
        sampleTime += frames;
        return result;
    }

    // The internal block itself with a list of events, as the base class's
    // renderBlock hands it what was scheduled.
    AUAudioUnitStatus renderEvents(AUAudioFrameCount frames,
                                   const AURenderEvent* events)
    {
        auto flags = AudioUnitRenderActionFlags {};
        timestamp = nextTimestamp();
        rendered = &outputs[0].list(frames);

        auto result =
            internal(&flags, &timestamp, frames, 0, rendered, events, pullBlock());
        sampleTime += frames;
        return result;
    }

    AURenderPullInputBlock pullBlock() const
    {
        auto result = AURenderPullInputBlock {};

        if (connectInputs)
            result = pullInput;

        return result;
    }

    float renderedSample(int channel, int sample) const
    {
        return static_cast<const float*>(rendered->mBuffers[channel].mData)[sample];
    }

    AUParameter* parameterFor(const Parameter& param) const
    {
        const auto& list = pluginPtr->parameters();
        return [unit.parameterTree
            parameterWithAddress:list.entry(list.indexOf(&param)).address];
    }

    AUAudioUnitStatus pull(AUAudioFrameCount frames, int bus, AudioBufferList* input)
    {
        auto& source = inputs[static_cast<size_t>(bus)].storage;
        auto bytes = static_cast<UInt32>(frames * sizeof(float));
        ++pulls;

        for (auto ch = UInt32 {0}; ch < input->mNumberBuffers; ++ch)
        {
            auto& buffer = input->mBuffers[ch];
            const auto* from = source[ch % source.size()].data();

            // In place: the host hands back its own output memory, so input and
            // output alias.
            if (inPlace && static_cast<size_t>(bus) < outputs.size())
            {
                auto* into = outputs[static_cast<size_t>(bus)].storage[ch].data();
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

    OSStatus receiveMidi(AUEventSampleTime time,
                         uint8_t cable,
                         NSInteger length,
                         const uint8_t* bytes)
    {
        if (sent.size() < sent.capacity())
        {
            auto entry = SentMidi {
                .bus = cable,
                .offset = static_cast<MIDITimeStamp>(time - timestamp.mSampleTime)};
            entry.length = static_cast<UInt16>(std::min<NSInteger>(
                length, static_cast<NSInteger>(entry.bytes.size())));
            std::copy_n(bytes, entry.length, entry.bytes.begin());
            sent.push_back(entry);
        }

        return noErr;
    }

    AUAudioUnit* unit = nil;
    P* pluginPtr = nullptr;

    bool connectInputs = true;
    bool collectMidi = true;
    bool inPlace = false;

    AURenderPullInputBlock pullInput = nil;
    AUMIDIOutputEventBlock midiOut = nil;
    AUInternalRenderBlock internal = nil;

    std::vector<HostBus> inputs;
    std::vector<HostBus> outputs;
    AudioBufferList* rendered = nullptr;

    AudioTimeStamp timestamp {};
    Float64 sampleTime = 0.0;
    int pulls = 0;

    std::vector<SentMidi> sent;
};

// A linked list of render events in fixed storage, built outside the ban and
// handed to the internal block as the base class would.
struct EventList
{
    // Room for an event per parameter of a consumer's plugin, plus its MIDI.
    static constexpr int capacity = 256;

    void clear() noexcept { count = 0; }

    void parameter(AUParameterAddress address, AUValue value, AUEventSampleTime time)
    {
        auto& event = next();
        event.parameter.eventType = AURenderEventParameter;
        event.parameter.eventSampleTime = time;
        event.parameter.parameterAddress = address;
        event.parameter.value = value;
    }

    void midi(AUEventSampleTime time, std::initializer_list<uint8_t> bytes)
    {
        auto& event = next();
        event.MIDI.eventType = AURenderEventMIDI;
        event.MIDI.eventSampleTime = time;
        event.MIDI.length = static_cast<uint16_t>(std::min<size_t>(bytes.size(), 3));
        std::copy_n(bytes.begin(), event.MIDI.length, event.MIDI.data);
    }

    const AURenderEvent* head() noexcept
    {
        for (auto i = 0; i < count; ++i)
            events[i].head.next = i + 1 < count ? &events[i + 1] : nullptr;

        return count > 0 ? &events[0] : nullptr;
    }

    AURenderEvent& next()
    {
        nano::check(count < capacity);
        auto& event = events[count++];
        event = AURenderEvent {};
        return event;
    }

    std::array<AURenderEvent, capacity> events {};
    int count = 0;
};

} // namespace AUv3Host
