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

    // Whether the platform's MIDI system came up at all. A machine whose MIDI
    // system refused to start is an ordinary state: the manager stays usable and
    // reports it rather than failing to construct.
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
    // platform has no virtual ports — Windows, and the iOS simulator, which
    // refuses them to a process with no bundle.
    std::optional<int> openVirtualInput(const std::string& name);
    std::optional<int> openVirtualInput(const std::string& name,
                                        const MidiInputCallback& cb);

    // How much of a SysEx dump an input port assembles before giving up on it.
    // The buffer is allocated when the port opens, so set this before openInput;
    // a longer dump is dropped and raises MidiNotification::SysExDropped. Queue
    // mode has a second, much smaller limit: a dump reaches drainMessages() only
    // when it fits MIDI::SysEx::maxBytes.
    void setMaxSysExBytes(int bytes);
    int getMaxSysExBytes() const;

    // Timing clock (0xF8) and active sensing (0xFE) are filtered by default;
    // pass false to either to have it delivered. Applies to ports already open.
    void setIgnoredTypes(bool clock, bool activeSense);

    void closeInput(int portId);
    void closeAllInputs();
    bool isInputOpen(int portId) const;
    Vector<int> getOpenInputPorts() const;

    // Audio-callback safe: `out` is pre-reserved so no allocation happens, and
    // each port hands its events over through a wait-free queue.
    void drainMessages(MidiEvents& out);

    Error openOutput(int portId);

    // Replaces any currently open output. No virtual ports on Windows.
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
