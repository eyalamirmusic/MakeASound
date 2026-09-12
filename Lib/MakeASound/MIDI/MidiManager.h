#pragma once

#include "../Common/Common.h"
#include "../Devices/DeviceInfo.h"
#include "MidiInfo.h"

#include <optional>

namespace MakeASound
{
class MidiBackend;

class MidiManager
{
public:
    MidiManager();
    ~MidiManager();

    // The clock every backend stamps MidiInputEvent::arrival on, so a host can
    // compare an arrival against a boundary it took itself.
    static MidiTimePoint now();

    Vector<MidiPortInfo> getInputPorts() const;
    Vector<MidiPortInfo> getOutputPorts() const;

    // Whether the platform's MIDI system came up at all. False on iOS, where the
    // ports below all fail; the manager stays usable and reports it.
    bool isAvailable() const;

    // Why the last call failed. Errors are returned rather than thrown, as on the
    // audio side — see Devices/DeviceInfo.h.
    Error getLastError() const;

    // Queue mode: events accumulate internally until drainMessages().
    Error openInput(int portId);

    // Callback mode: `cb` fires on the platform's MIDI thread, nothing is queued.
    // The message is a per-port buffer refilled by the next one, so copy what you
    // keep.
    Error openInput(int portId, const MidiInputCallback& cb);

    // A synthetic (negative) portId, usable like a real one, or nullopt where the
    // platform has no virtual ports — Windows and iOS.
    std::optional<int> openVirtualInput(const std::string& name);
    std::optional<int> openVirtualInput(const std::string& name,
                                        const MidiInputCallback& cb);

    void closeInput(int portId);
    void closeAllInputs();
    bool isInputOpen(int portId) const;
    Vector<int> getOpenInputPorts() const;

    // Audio-callback safe: `out` is pre-reserved so no allocation happens,
    // and ports whose spinlock is contended are skipped until the next call.
    void drainMessages(MidiEvents& out);

    Error openOutput(int portId);

    // Replaces any currently open output. No virtual ports on Windows or iOS.
    Error openVirtualOutput(const std::string& name);

    void closeOutput();
    bool isOutputOpen() const;

    // INVALID_USE with no output open, which used to be a silent no-op.
    Error sendMessage(const MidiMessage& message);
    Error sendMessage(const std::uint8_t* bytes, std::size_t size);
    Error sendMessage(const MIDI::Event& event);

    // Runs on whatever platform thread raised the notification, so treat it like
    // the audio one and do no work there. Prefer drainNotifications() unless the
    // delivery has to be immediate.
    void setNotificationCallback(const MidiNotificationCallback& cb) const;

    // The same notifications, queued instead of delivered: everything since the
    // last call, in order; past 64 undrained the newest are dropped.
    Vector<MidiNotification> drainNotifications() const;

private:
    OwningPointer<MidiBackend> pimpl;
};

} // namespace MakeASound
