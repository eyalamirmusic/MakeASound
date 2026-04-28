#pragma once

#include "../MidiInfo/MidiInfo.h"
#include <memory>

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

    std::vector<MidiPortInfo> getInputPorts() const;
    std::vector<MidiPortInfo> getOutputPorts() const;

    // Open a port in queue mode. Incoming events accumulate in an internal
    // ring and surface via drainMessages().
    void openInput(unsigned int portId);

    // Open a port in callback mode. The callback fires on RtMidi's input
    // thread; events are not queued.
    void openInput(unsigned int portId, const MidiInputCallback& cb);

    void closeInput(unsigned int portId);
    void closeAllInputs();
    bool isInputOpen(unsigned int portId) const;
    std::vector<unsigned int> getOpenInputPorts() const;

    // Drain all queued events from queue-mode ports. Designed to be called
    // from an audio callback: each port is guarded by a spinlock that we
    // try-lock; contended ports are skipped and drained next call. The
    // MidiEvents wrapper pre-reserves capacity so audio-thread pushes
    // don't allocate.
    void drainMessages(MidiEvents& out);

    void openOutput(unsigned int portId);
    void closeOutput();
    bool isOutputOpen() const;

    void sendMessage(const MidiMessage& message);
    void sendMessage(const std::uint8_t* bytes, std::size_t size);

private:
    std::unique_ptr<RTMidi::MidiManager> pimpl;
};

} // namespace MakeASound
