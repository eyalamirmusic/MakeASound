#include "SystemOutputTap.h"

#include <CoreAudio/AudioHardwareTapping.h>
#include <CoreAudio/CATapDescription.h>
#include <CoreAudio/CoreAudio.h>
#import <Foundation/Foundation.h>

namespace MakeASound
{

namespace
{
NSString* getDefaultOutputDeviceUid()
{
    auto address = AudioObjectPropertyAddress {
        kAudioHardwarePropertyDefaultOutputDevice,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain};

    auto deviceId = AudioObjectID {kAudioObjectUnknown};
    auto size = static_cast<UInt32>(sizeof(deviceId));

    if (AudioObjectGetPropertyData(
            kAudioObjectSystemObject, &address, 0, nullptr, &size, &deviceId)
        != noErr)
        return nil;

    address.mSelector = kAudioDevicePropertyDeviceUID;
    auto uid = CFStringRef {};
    size = sizeof(uid);

    if (AudioObjectGetPropertyData(deviceId, &address, 0, nullptr, &size, &uid)
        != noErr)
        return nil;

    return CFBridgingRelease(uid);
}

NSDictionary* makeAggregateDescription(NSString* aggregateUid,
                                       NSString* tapUid,
                                       NSString* clockDeviceUid)
{
    auto* tap = @{
        @kAudioSubTapUIDKey: tapUid,
        @kAudioSubTapDriftCompensationKey: @YES,
    };

    auto* description = [NSMutableDictionary dictionaryWithDictionary:@{
        @kAudioAggregateDeviceNameKey: @"MakeASound System Output Capture",
        @kAudioAggregateDeviceUIDKey: aggregateUid,
        @kAudioAggregateDeviceIsPrivateKey: @YES,
        @kAudioAggregateDeviceIsStackedKey: @NO,
        @kAudioAggregateDeviceTapAutoStartKey: @YES,
        @kAudioAggregateDeviceTapListKey: @[tap],
    }];

    // The output device clocks the aggregate, so the tap is read at its rate.
    if (clockDeviceUid != nil)
    {
        description[@kAudioAggregateDeviceMainSubDeviceKey] = clockDeviceUid;
        description[@kAudioAggregateDeviceSubDeviceListKey] =
            @[@{@kAudioSubDeviceUIDKey: clockDeviceUid}];
    }

    return description;
}
} // namespace

SystemOutputTap::~SystemOutputTap()
{
    destroy();
}

Error SystemOutputTap::create()
{
    destroy();

    if (@available(macOS 14.2, *))
    {
        @autoreleasepool
        {
            auto* tapDescription =
                [[CATapDescription alloc] initStereoGlobalTapButExcludeProcesses:@[]];
            tapDescription.privateTap = YES;
            tapDescription.muteBehavior = CATapUnmuted;

            auto createdTapId = AudioObjectID {kAudioObjectUnknown};

            if (AudioHardwareCreateProcessTap(tapDescription, &createdTapId) != noErr)
                return Error::SYSTEM_ERROR;

            tapId = createdTapId;

            auto* aggregateUid = [[NSUUID UUID] UUIDString];
            auto* description = makeAggregateDescription(
                aggregateUid, tapDescription.UUID.UUIDString, getDefaultOutputDeviceUid());

            auto createdDeviceId = AudioObjectID {kAudioObjectUnknown};
            auto status = AudioHardwareCreateAggregateDevice(
                (__bridge CFDictionaryRef) description, &createdDeviceId);

            if (status != noErr)
            {
                destroy();
                return Error::SYSTEM_ERROR;
            }

            aggregateDeviceId = createdDeviceId;
            captureDeviceUid = aggregateUid.UTF8String;
            return Error::NoError;
        }
    }

    return Error::NOT_SUPPORTED;
}

void SystemOutputTap::destroy()
{
    if (@available(macOS 14.2, *))
    {
        if (aggregateDeviceId != 0)
            AudioHardwareDestroyAggregateDevice(aggregateDeviceId);

        if (tapId != 0)
            AudioHardwareDestroyProcessTap(tapId);
    }

    aggregateDeviceId = 0;
    tapId = 0;
    captureDeviceUid.clear();
}

bool SystemOutputTap::isCreated() const
{
    return aggregateDeviceId != 0;
}

const std::string& SystemOutputTap::getCaptureDeviceUid() const
{
    return captureDeviceUid;
}

} // namespace MakeASound
