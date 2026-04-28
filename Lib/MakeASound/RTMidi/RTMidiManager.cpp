#include "RTMidiManager.h"

#include <stdexcept>

namespace MakeASound::RTMidi
{

MidiManager::MidiManager()
    : input(std::make_unique<::RtMidiIn>())
    , output(std::make_unique<::RtMidiOut>())
{
}

std::vector<MidiPortInfo> MidiManager::getInputPorts()
{
    return getPorts(*input);
}

std::vector<MidiPortInfo> MidiManager::getOutputPorts()
{
    return getPorts(*output);
}

void MidiManager::openInput(unsigned int portId, const MidiInputCallback& cb)
{
    closeInput();

    callback = cb;
    input->setCallback(midiInputTrampoline, this);
    input->openPort(portId);
}

void MidiManager::closeInput()
{
    if (input->isPortOpen())
        input->closePort();

    input->cancelCallback();
    callback = nullptr;
}

bool MidiManager::isInputOpen() const
{
    return input->isPortOpen();
}

void MidiManager::openOutput(unsigned int portId)
{
    closeOutput();
    output->openPort(portId);
}

void MidiManager::closeOutput()
{
    if (output->isPortOpen())
        output->closePort();
}

bool MidiManager::isOutputOpen() const
{
    return output->isPortOpen();
}

void MidiManager::sendMessage(const std::uint8_t* bytes, std::size_t size)
{
    output->sendMessage(bytes, size);
}

void midiInputTrampoline(double timestamp,
                         std::vector<unsigned char>* message,
                         void* userData)
{
    if (message == nullptr || userData == nullptr)
        return;

    auto& manager = *static_cast<MidiManager*>(userData);

    if (!manager.callback)
        return;

    manager.callback(getMessage(timestamp, *message));
}

} // namespace MakeASound::RTMidi
