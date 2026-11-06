// The notification callback is installed from the host's thread and invoked from
// whichever OS thread raised the notification (a Core Audio property listener,
// miniaudio's own worker, the recovery thread). These cases drive the backend's real
// notification entry point, onNotification, the way the allocation suites drive
// onCallback: no device has to exist, so they run on a headless runner too.

#include <MakeASound/MakeASound.h>
#include <MakeASound/MiniAudio/MiniAudioDeviceManager.h>

#include <NanoTest/NanoTest.h>

#include <array>
#include <atomic>
#include <thread>

namespace MiniAudioNotificationTests
{
using namespace nano;
using MakeASound::DeviceNotification;
using MakeASound::NotificationCallback;
using Backend = MakeASound::MiniAudio::DeviceManager;

namespace
{
// Under the 64 the queue holds, so every notification of a burst is still there
// when the burst is drained.
constexpr auto kBurst = 32;
constexpr auto kBursts = 400;

void install(Backend& backend, const NotificationCallback& cb)
{
    backend.setNotificationCallback(cb);
}

// Rerouted, because a Stopped would hand the recovery worker a device to re-open.
void raise(Backend& backend)
{
    backend.onNotification(ma_device_notification_type_rerouted);
}

bool allRerouted(const MakeASound::Vector<DeviceNotification>& notifications)
{
    for (auto notification: notifications)
        if (notification != DeviceNotification::Rerouted)
            return false;

    return true;
}

// Bigger than any std::function's inline buffer, so every install allocates and
// every replacement frees: a reader racing the swap touches freed memory, not a
// stale but intact copy.
NotificationCallback makeCounter(std::atomic<int>& count)
{
    auto padding = std::array<char, 128> {};

    return [&count, padding](DeviceNotification)
    {
        if (padding[0] == 0)
            ++count;
    };
}

auto tSwapWhileNotifying =
    test("DeviceManager/swapsTheNotificationCallbackWhileOneIsDelivered") = []
{
    auto backend = Backend {};
    backend.autoRecover = false;

    auto swapped = std::atomic<int> {0};
    auto finished = std::atomic<bool> {false};

    // The host's thread, setting and clearing the callback as fast as it can.
    auto swapper = std::thread(
        [&]
        {
            while (!finished)
            {
                install(backend, makeCounter(swapped));
                install(backend, {});
            }
        });

    auto allQueued = true;

    for (auto burst = 0; burst < kBursts; ++burst)
    {
        for (auto i = 0; i < kBurst; ++i)
            raise(backend);

        auto drained = backend.takeNotifications();
        allQueued = allQueued && drained.size() == kBurst && allRerouted(drained);
    }

    finished = true;
    swapper.join();

    // The queue never depended on the callback: every notification was there.
    check(allQueued);

    // Whatever the swapper left installed, a callback set now hears every
    // notification raised after it, and the queue still gets each one too.
    auto counted = std::atomic<int> {0};
    install(backend, makeCounter(counted));

    for (auto i = 0; i < kBurst; ++i)
        raise(backend);

    check(counted == kBurst);
    check(backend.takeNotifications().size() == kBurst);

    // Cleared, it hears nothing more, and the queue is unaffected.
    install(backend, {});

    for (auto i = 0; i < kBurst; ++i)
        raise(backend);

    check(counted == kBurst);
    check(backend.takeNotifications().size() == kBurst);
};

auto tCallbackCanReenter =
    test("DeviceManager/notificationCallbackMayCallBackIntoTheManager") = []
{
    // Nothing is held while the host's callback runs, so it may replace itself or
    // drain the queue from inside: neither deadlocks.
    auto backend = Backend {};
    backend.autoRecover = false;

    auto calls = 0;
    auto drainedInside = 0;

    install(backend,
            [&](DeviceNotification)
            {
                ++calls;
                drainedInside += backend.takeNotifications().size();
                install(backend, {});
            });

    raise(backend);
    raise(backend);

    check(calls == 1);
    check(drainedInside == 1);
    check(backend.takeNotifications().size() == 1);
};
} // namespace
} // namespace MiniAudioNotificationTests
