#pragma once

#include "RTMidi-Backend.h"

namespace MakeASound::RTMidi
{

void midiInputTrampoline(double timestamp,
                         std::vector<unsigned char>* message,
                         void* userData);

struct InputPort
{
    InputPort() { queue.reserve(256); }

    int portId {};
    OwningPointer<::RtMidiIn> rtIn;
    MidiInputCallback callback;

    EA::Locks::PrimitiveSpinLock lock;
    Vector<MidiInputEvent> queue;
};

struct MidiManager
{
    MidiManager();

    Vector<MidiPortInfo> getInputPorts();
    Vector<MidiPortInfo> getOutputPorts();

    void openInput(int portId, const MidiInputCallback& cb);
    void closeInput(int portId);
    void closeAllInputs();
    bool isInputOpen(int portId) const;
    Vector<int> getOpenInputPorts() const;
    void drainMessages(Vector<MidiInputEvent>& out);

    void openOutput(int portId);
    void closeOutput();
    bool isOutputOpen() const;

    void sendMessage(const std::uint8_t* bytes, std::size_t size);

private:
    OwningPointer<::RtMidiIn> inputEnumerator;
    OwningPointer<::RtMidiOut> outputEnumerator;
    OwningPointer<::RtMidiOut> output;
    EA::OwnedVector<InputPort> inputs;
};

} // namespace MakeASound::RTMidi
