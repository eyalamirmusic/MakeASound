#include "MidiBackend.h"

namespace MakeASound
{
namespace
{
constexpr auto kMaxPendingNotifications = 64;
}

MidiBackend::MidiBackend()
{
    pendingNotifications.reserve(kMaxPendingNotifications);
}

void MidiBackend::notifyHost(MidiNotification notification)
{
    {
        auto lock = std::lock_guard(notificationMutex);

        if (pendingNotifications.size() < kMaxPendingNotifications)
            pendingNotifications.add(notification);
    }

    if (notificationCallback)
        notificationCallback(notification);
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
