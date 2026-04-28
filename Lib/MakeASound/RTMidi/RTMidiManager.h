#pragma once

#include "RTMidi-Backend.h"
#include <memory>

namespace MakeASound::RTMidi
{

void midiInputTrampoline(double timestamp,
                         std::vector<unsigned char>* message,
                         void* userData);

struct MidiManager
{
    MidiManager();

    std::vector<MidiPortInfo> getInputPorts();
    std::vector<MidiPortInfo> getOutputPorts();

    void openInput(unsigned int portId, const MidiInputCallback& cb);
    void closeInput();
    bool isInputOpen() const;

    void openOutput(unsigned int portId);
    void closeOutput();
    bool isOutputOpen() const;

    void sendMessage(const std::uint8_t* bytes, std::size_t size);

    MidiInputCallback callback;

private:
    std::unique_ptr<::RtMidiIn> input;
    std::unique_ptr<::RtMidiOut> output;
};

} // namespace MakeASound::RTMidi
