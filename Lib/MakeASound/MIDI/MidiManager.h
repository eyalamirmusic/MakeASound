#pragma once

#include "../Common/Common.h"
#include "../Devices/DeviceInfo.h"
#include "MidiInfo.h"

#include <optional>

namespace MakeASound
{
namespace RTMidi
{
struct MidiManager;
}

class MidiManager
{
public:
    MidiManager();
    ~MidiManager();

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

    // Callback mode: `cb` fires on RtMidi's input thread, nothing is queued.
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

private:
    OwningPointer<RTMidi::MidiManager> pimpl;
};

} // namespace MakeASound
