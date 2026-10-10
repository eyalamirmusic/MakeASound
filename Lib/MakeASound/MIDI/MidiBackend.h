#pragma once

#include "../Common/Common.h"
#include "../Devices/DeviceInfo.h"
#include "MidiInfo.h"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

namespace MakeASound
{

// Everything MidiManager needs a platform to answer. One implementation per
// platform, picked at link time by makeMidiBackend() the way DeviceQueries is, so
// shared code holds no #ifdefs.
//
// Port ids are registry slots (see MidiPortRegistry), not enumeration positions,
// and MidiInputEvent::arrival is stamped on MidiManager::now()'s clock.
class MidiBackend
{
public:
    MidiBackend();
    virtual ~MidiBackend() = default;

    virtual Vector<MidiPortInfo> getInputPorts() = 0;
    virtual Vector<MidiPortInfo> getOutputPorts() = 0;

    virtual bool isAvailable() const = 0;
    virtual Error getLastError() const = 0;

    // An empty callback is queue mode: events wait for drainMessages() instead of
    // being handed over on the platform's MIDI thread.
    virtual Error openInput(int portId, const MidiInputCallback& cb) = 0;
    virtual std::optional<int> openVirtualInput(const std::string& name,
                                                const MidiInputCallback& cb) = 0;

    virtual void closeInput(int portId) = 0;
    virtual void closeAllInputs() = 0;
    virtual bool isInputOpen(int portId) const = 0;
    virtual Vector<int> getOpenInputPorts() const = 0;
    virtual void drainMessages(Vector<MidiInputEvent>& out) = 0;

    virtual Error openOutput(int portId) = 0;
    virtual Error openVirtualOutput(const std::string& name) = 0;
    virtual void closeOutput() = 0;
    virtual bool isOutputOpen() const = 0;

    virtual Error sendMessage(const std::uint8_t* bytes, std::size_t size) = 0;

    // The assembly buffer a port gives a SysEx dump. It is allocated when the
    // port opens, so this only reaches ports opened after the call; anything
    // longer than it is dropped with a SysExDropped notification.
    static constexpr int defaultMaxSysExBytes = 64 * 1024;

    void setMaxSysExBytes(int bytes);
    int getMaxSysExBytes() const { return maxSysExBytes; }

    // Timing clock and active sensing are filtered by default. Every backend
    // overrides this to reach the ports it already has open, then calls here to
    // store the flags for the next one.
    virtual void setIgnoredTypes(bool clock, bool activeSense);

    // Delivered on whatever platform thread raised the notification, so it is
    // installed under the same lock that guards the queue: a host swapping it
    // races a hotplug otherwise.
    void setNotificationCallback(const MidiNotificationCallback& cb);

    // Everything queued since the last call, in order, for whatever thread asks.
    Vector<MidiNotification> takeNotifications();

protected:
    // Both paths at once, so a backend raises a notification in one place: the
    // callback now, and the queue for a host thread that would rather not be this
    // one.
    void notifyHost(MidiNotification notification);

    int maxSysExBytes {defaultMaxSysExBytes};
    bool ignoreClock {true};
    bool ignoreActiveSense {true};

private:
    mutable std::mutex notificationMutex;
    MidiNotificationCallback notificationCallback;
    Vector<MidiNotification> pendingNotifications;
};

OwningPointer<MidiBackend> makeMidiBackend();

} // namespace MakeASound
