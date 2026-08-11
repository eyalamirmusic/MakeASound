#pragma once

#include "../Common/Common.h"
#include "MidiInfo.h"

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

    // Queue mode: events accumulate internally until drainMessages().
    void openInput(int portId);

    // Callback mode: `cb` fires on RtMidi's input thread, nothing is queued.
    void openInput(int portId, const MidiInputCallback& cb);

    // Returns a synthetic (negative) portId, usable like a real one.
    // Virtual ports exist only on CoreMIDI / ALSA / JACK — throws on Windows.
    int openVirtualInput(const std::string& name);
    int openVirtualInput(const std::string& name,
                         const MidiInputCallback& cb);

    void closeInput(int portId);
    void closeAllInputs();
    bool isInputOpen(int portId) const;
    Vector<int> getOpenInputPorts() const;

    // Audio-callback safe: `out` is pre-reserved so no allocation happens,
    // and ports whose spinlock is contended are skipped until the next call.
    void drainMessages(MidiEvents& out);

    void openOutput(int portId);

    // Replaces any currently open output. Throws on Windows.
    void openVirtualOutput(const std::string& name);

    void closeOutput();
    bool isOutputOpen() const;

    void sendMessage(const MidiMessage& message);
    void sendMessage(const std::uint8_t* bytes, std::size_t size);

    void sendMessage(const MIDI::Event& event);

private:
    OwningPointer<RTMidi::MidiManager> pimpl;
};

} // namespace MakeASound
