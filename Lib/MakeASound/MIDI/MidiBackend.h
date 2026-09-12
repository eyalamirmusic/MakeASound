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

    // Delivered on whatever platform thread raised the notification.
    MidiNotificationCallback notificationCallback;

    // Everything queued since the last call, in order, for whatever thread asks.
    Vector<MidiNotification> takeNotifications();

protected:
    // Both paths at once, so a backend raises a notification in one place: the
    // callback now, and the queue for a host thread that would rather not be this
    // one.
    void notifyHost(MidiNotification notification);

private:
    std::mutex notificationMutex;
    Vector<MidiNotification> pendingNotifications;
};

OwningPointer<MidiBackend> makeMidiBackend();

} // namespace MakeASound
