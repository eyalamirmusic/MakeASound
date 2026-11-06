// AUv3Probe <app or appex>...: what a host does with an installed AUv3, for each
// AudioComponents entry of each extension named (an app stands for every
// PlugIns/*.appex inside it). The component must be registered with the system,
// instantiate out of process, render sound (an effect fed a sine, an instrument
// a note) and hand back a view controller. Run it through `xcrun simctl spawn`
// once the apps are installed; it prints PASS or FAIL per component and exits with
// the number that failed, or 1 with nothing to probe.
#import <AVFoundation/AVFoundation.h>
#import <AudioToolbox/AudioToolbox.h>
#import <CoreAudioKit/CoreAudioKit.h>
#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace
{
constexpr auto blockSize = AUAudioFrameCount {512};
constexpr auto blocks = 16;
constexpr auto timeoutSeconds = 30.0;

struct Component
{
    AudioComponentDescription description {};
    std::string name;
};

void say(const std::string& text)
{
    std::printf("%s\n", text.c_str());
    std::fflush(stdout);
}

std::string str(NSString* text)
{
    return text != nil ? std::string(text.UTF8String) : std::string {};
}

OSType fourCC(NSString* text)
{
    auto result = OSType {};

    if (text.length != 4)
        return result;

    for (auto i = 0; i < 4; ++i)
        result =
            (result << 8) | static_cast<OSType>([text characterAtIndex:i] & 0xFF);

    return result;
}

// Runs the main run loop until done() or the timeout; false on the timeout.
bool runUntil(const std::function<bool()>& done, double seconds = timeoutSeconds)
{
    auto* deadline = [NSDate dateWithTimeIntervalSinceNow:seconds];

    while (!done())
    {
        if ([deadline timeIntervalSinceNow] < 0)
            return false;

        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, false);
    }

    return true;
}

void addComponentsOf(NSString* appex, std::vector<Component>& components)
{
    auto* plist =
        [NSDictionary dictionaryWithContentsOfFile:
                          [appex stringByAppendingPathComponent:@"Info.plist"]];
    NSArray* entries =
        plist[@"NSExtension"][@"NSExtensionAttributes"][@"AudioComponents"];

    if (entries.count == 0)
        say("no AudioComponents in " + str(appex));

    for (NSDictionary* entry in entries)
    {
        auto component = Component {};
        component.description.componentType = fourCC(entry[@"type"]);
        component.description.componentSubType = fourCC(entry[@"subtype"]);
        component.description.componentManufacturer = fourCC(entry[@"manufacturer"]);
        component.name = str(entry[@"name"]) + " (" + str(entry[@"type"]) + " "
                         + str(entry[@"subtype"]) + " " + str(entry[@"manufacturer"])
                         + ")";
        components.push_back(component);
    }
}

std::vector<Component> componentsIn(NSString* path)
{
    auto components = std::vector<Component> {};

    if ([path.pathExtension isEqualToString:@"appex"])
    {
        addComponentsOf(path, components);
        return components;
    }

    auto* plugIns = [path stringByAppendingPathComponent:@"PlugIns"];
    auto* names = [[NSFileManager defaultManager] contentsOfDirectoryAtPath:plugIns
                                                                      error:nil];

    for (NSString* name in names)
        if ([name.pathExtension isEqualToString:@"appex"])
            addComponentsOf([plugIns stringByAppendingPathComponent:name],
                            components);

    if (components.empty())
        say("no extension in " + str(path));

    return components;
}

// The registrar may take a moment to see an extension just installed.
bool isRegistered(const Component& component)
{
    auto description = component.description;
    auto found = AudioComponent {};
    auto registered = runUntil(
        [&]
        {
            found = AudioComponentFindNext(nullptr, &description);
            return found != nullptr;
        });

    if (!registered)
    {
        say("  not registered");
        return false;
    }

    auto actual = AudioComponentDescription {};
    AudioComponentGetDescription(found, &actual);
    auto isV3 = (actual.componentFlags & kAudioComponentFlag_IsV3AudioUnit) != 0;
    char flags[16];
    std::snprintf(flags, sizeof(flags), "%x", actual.componentFlags);
    say(std::string("  registered, flags 0x") + flags
        + (isV3 ? " (v3)" : " (not v3)"));

    return isV3;
}

AUAudioUnit* instantiate(const Component& component)
{
    __block AUAudioUnit* result = nil;
    auto finished = false;
    auto* done = &finished;

    [AUAudioUnit
        instantiateWithComponentDescription:component.description
                                    options:0
                          completionHandler:^(AUAudioUnit* unit, NSError* error) {
                            result = unit;
                            *done = true;

                            if (unit == nil)
                                say("  instantiation failed: "
                                    + str(error.description));
                          }];

    if (!runUntil([&] { return finished; }))
        say("  instantiation timed out");

    if (result != nil)
        say("  instantiated " + str(NSStringFromClass([result class])) + ", "
            + std::to_string(result.inputBusses.count) + " in, "
            + std::to_string(result.outputBusses.count) + " out, "
            + std::to_string(result.parameterTree.allParameters.count)
            + " parameters");

    return result;
}

// An effect hears a 440 Hz sine at half scale and an instrument plays middle C;
// either way the output must carry sound.
bool renders(AUAudioUnit* unit, bool isInstrument)
{
    unit.maximumFramesToRender = blockSize;
    auto* error = (NSError*) nil;

    if (![unit allocateRenderResourcesAndReturnError:&error])
    {
        say("  allocateRenderResources failed: " + str(error.description));
        return false;
    }

    auto* format = unit.outputBusses[0].format;
    auto channels = static_cast<int>(format.channelCount);
    auto rate = format.sampleRate;
    auto output =
        std::vector<std::vector<float>>(channels, std::vector<float>(blockSize));
    auto input = std::vector<std::vector<float>>(8, std::vector<float>(blockSize));
    auto storage =
        std::vector<char>(sizeof(AudioBufferList)
                          + sizeof(AudioBuffer) * static_cast<size_t>(channels));
    auto* list = reinterpret_cast<AudioBufferList*>(storage.data());
    auto* inputs = &input;
    __block auto phase = 0.0;

    auto pull = ^AUAudioUnitStatus(AudioUnitRenderActionFlags*,
                                   const AudioTimeStamp*,
                                   AUAudioFrameCount frames,
                                   NSInteger,
                                   AudioBufferList* data) {
      auto start = phase;

      for (auto b = 0u; b < data->mNumberBuffers && b < inputs->size(); ++b)
      {
          auto& channel = (*inputs)[b];

          for (auto i = 0u; i < frames; ++i)
              channel[i] = 0.5f
                           * static_cast<float>(
                               std::sin(start + 2.0 * M_PI * 440.0 * i / rate));

          if (data->mBuffers[b].mData == nullptr)
              data->mBuffers[b].mData = channel.data();
          else
              std::copy_n(channel.data(), frames, (float*) data->mBuffers[b].mData);

          data->mBuffers[b].mDataByteSize = frames * sizeof(float);
      }

      phase = start + 2.0 * M_PI * 440.0 * frames / rate;
      return noErr;
    };

    if (isInstrument)
    {
        auto midi = unit.scheduleMIDIEventBlock;

        if (midi == nil)
        {
            say("  no scheduleMIDIEventBlock");
            return false;
        }

        const uint8_t noteOn[] = {0x90, 60, 100};
        midi(AUEventSampleTimeImmediate, 0, sizeof(noteOn), noteOn);
    }

    auto render = unit.renderBlock;
    auto time = AudioTimeStamp {};
    time.mFlags = kAudioTimeStampSampleTimeValid;
    auto peak = 0.0f;
    auto status = OSStatus {};

    for (auto b = 0; b < blocks && status == noErr; ++b)
    {
        list->mNumberBuffers = static_cast<UInt32>(channels);

        for (auto c = 0; c < channels; ++c)
            list->mBuffers[c] = {1, blockSize * sizeof(float), output[c].data()};

        auto flags = AudioUnitRenderActionFlags {};
        status = render(&flags, &time, blockSize, 0, list, pull);
        time.mSampleTime += blockSize;

        for (auto c = 0; c < channels; ++c)
        {
            auto* samples = static_cast<const float*>(list->mBuffers[c].mData);

            for (auto i = 0u; i < blockSize; ++i)
                peak = std::max(peak, std::fabs(samples[i]));
        }
    }

    [unit deallocateRenderResources];

    char line[96];
    std::snprintf(
        line,
        sizeof(line),
        "  rendered %d x %u frames at %.0f Hz, %d channels: status %d, peak %.3f",
        blocks,
        blockSize,
        rate,
        channels,
        static_cast<int>(status),
        peak);
    say(line);

    return status == noErr && peak > 0.01f;
}

bool showsView(AUAudioUnit* unit)
{
    __block UIViewController* result = nil;
    auto finished = false;
    auto* done = &finished;

    [unit
        requestViewControllerWithCompletionHandler:^(UIViewController* controller) {
          result = controller;
          *done = true;
        }];

    if (!runUntil([&] { return finished; }))
    {
        say("  view controller timed out");
        return false;
    }

    if (result == nil)
    {
        say("  no view controller");
        return false;
    }

    // Loading the remote view has the extension load its controller's, which
    // opens the editor and states the editor's size.
    auto* controller = result;
    auto before = controller.preferredContentSize;
    [controller loadViewIfNeeded];
    runUntil([&]
             { return !CGSizeEqualToSize(controller.preferredContentSize, before); },
             5);

    auto size = controller.preferredContentSize;
    say("  view controller " + str(NSStringFromClass([controller class]))
        + ", preferred " + std::to_string(static_cast<int>(size.width)) + " x "
        + std::to_string(static_cast<int>(size.height)));

    return true;
}

bool probe(const Component& component)
{
    say(component.name);

    if (!isRegistered(component))
        return false;

    auto* unit = instantiate(component);

    if (unit == nil)
        return false;

    auto isInstrument =
        component.description.componentType == kAudioUnitType_MusicDevice;
    auto rendered = renders(unit, isInstrument);
    auto viewed = showsView(unit);

    return rendered && viewed;
}
} // namespace

int main(int argc, char* argv[])
{
    @autoreleasepool
    {
        auto components = std::vector<Component> {};

        for (auto i = 1; i < argc; ++i)
        {
            auto found = componentsIn(@(argv[i]));
            components.insert(components.end(), found.begin(), found.end());
        }

        if (components.empty())
        {
            std::fprintf(stderr, "usage: %s <app or appex>...\n", argv[0]);
            return 1;
        }

        auto failed = 0;

        for (auto& component: components)
        {
            auto passed = probe(component);
            say(std::string(passed ? "PASS " : "FAIL ") + component.name);
            failed += passed ? 0 : 1;
        }

        return failed;
    }
}
