#pragma once

#include "RTMidi-Backend.h"

#include <ea_data_structures/ea_data_structures.h>

#include <memory>
#include <vector>

namespace MakeASound::RTMidi
{

void midiInputTrampoline(double timestamp,
                         std::vector<unsigned char>* message,
                         void* userData);

struct InputPort
{
    InputPort() { queue.reserve(256); }

    unsigned int portId {};
    std::unique_ptr<::RtMidiIn> rtIn;
    MidiInputCallback callback;

    EA::Locks::PrimitiveSpinLock lock;
    std::vector<MidiMessage> queue;
};

struct MidiManager
{
    MidiManager();

    std::vector<MidiPortInfo> getInputPorts();
    std::vector<MidiPortInfo> getOutputPorts();

    void openInput(unsigned int portId, const MidiInputCallback& cb);
    void closeInput(unsigned int portId);
    void closeAllInputs();
    bool isInputOpen(unsigned int portId) const;
    std::vector<unsigned int> getOpenInputPorts() const;
    void drainMessages(std::vector<MidiInputEvent>& out);

    void openOutput(unsigned int portId);
    void closeOutput();
    bool isOutputOpen() const;

    void sendMessage(const std::uint8_t* bytes, std::size_t size);

private:
    std::unique_ptr<::RtMidiIn> inputEnumerator;
    std::unique_ptr<::RtMidiOut> outputEnumerator;
    std::unique_ptr<::RtMidiOut> output;
    std::vector<std::unique_ptr<InputPort>> inputs;
};

} // namespace MakeASound::RTMidi
