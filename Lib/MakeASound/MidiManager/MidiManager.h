#pragma once

#include "../Common/Common.h"
#include "../MidiInfo/MidiInfo.h"

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

    // Open a port in queue mode. Incoming events accumulate in an internal
    // ring and surface via drainMessages().
    void openInput(int portId);

    // Open a port in callback mode. The callback fires on RtMidi's input
    // thread; events are not queued.
    void openInput(int portId, const MidiInputCallback& cb);

    // Open a virtual input port visible to other apps under `name`.
    // Returns the synthetic (negative) portId assigned by the backend;
    // pass it to closeInput / isInputOpen exactly like a real portId.
    // RtMidi only supports virtual ports on CoreMIDI / ALSA / JACK —
    // this throws on Windows.
    int openVirtualInput(const std::string& name);
    int openVirtualInput(const std::string& name,
                         const MidiInputCallback& cb);

    void closeInput(int portId);
    void closeAllInputs();
    bool isInputOpen(int portId) const;
    Vector<int> getOpenInputPorts() const;

    // Drain all queued events from queue-mode ports. Designed to be called
    // from an audio callback: each port is guarded by a spinlock that we
    // try-lock; contended ports are skipped and drained next call. The
    // MidiEvents wrapper pre-reserves capacity so audio-thread pushes
    // don't allocate.
    void drainMessages(MidiEvents& out);

    void openOutput(int portId);

    // Open the output as a virtual port visible to other apps under
    // `name`. Replaces any currently open output. Throws on Windows.
    void openVirtualOutput(const std::string& name);

    void closeOutput();
    bool isOutputOpen() const;

    void sendMessage(const MidiMessage& message);
    void sendMessage(const std::uint8_t* bytes, std::size_t size);

private:
    OwningPointer<RTMidi::MidiManager> pimpl;
};

} // namespace MakeASound
