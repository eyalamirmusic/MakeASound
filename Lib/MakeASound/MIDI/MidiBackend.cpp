#include "MidiBackend.h"

#include <algorithm>

namespace MakeASound
{
namespace
{
constexpr auto kMaxPendingNotifications = 64;

// Room for a status byte and two data bytes, so the callback's buffer is never
// too small for the shortest message even when a host asks for nothing.
constexpr auto kMinSysExBytes = 8;
} // namespace

MidiBackend::MidiBackend()
{
    pendingNotifications.reserve(kMaxPendingNotifications);
}

void MidiBackend::setMaxSysExBytes(int bytes)
{
    maxSysExBytes = std::max(bytes, kMinSysExBytes);
}

void MidiBackend::setIgnoredTypes(bool clock, bool activeSense)
{
    ignoreClock = clock;
    ignoreActiveSense = activeSense;
}

void MidiBackend::setNotificationCallback(const MidiNotificationCallback& cb)
{
    auto lock = std::lock_guard(notificationMutex);
    notificationCallback = cb;
}

void MidiBackend::notifyHost(MidiNotification notification)
{
    auto callback = MidiNotificationCallback {};

    {
        auto lock = std::lock_guard(notificationMutex);

        if (pendingNotifications.size() < kMaxPendingNotifications)
            pendingNotifications.add(notification);

        // A copy, so the host's callback runs with nothing held: it is free to
        // call back into the manager from inside it.
        callback = notificationCallback;
    }

    if (callback)
        callback(notification);
}

Vector<MidiNotification> MidiBackend::takeNotifications()
{
    auto lock = std::lock_guard(notificationMutex);

    // A copy rather than a move, so the queue keeps the capacity it was given.
    auto taken = pendingNotifications;
    pendingNotifications.clear();

    return taken;
}

} // namespace MakeASound
