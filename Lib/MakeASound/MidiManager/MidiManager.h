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

    void openInput(unsigned int portId, const MidiInputCallback& cb);
    void closeInput();
    bool isInputOpen() const;

    void openOutput(unsigned int portId);
    void closeOutput();
    bool isOutputOpen() const;

    void sendMessage(const MidiMessage& message);
    void sendMessage(const std::uint8_t* bytes, std::size_t size);

private:
    std::unique_ptr<RTMidi::MidiManager> pimpl;
};

} // namespace MakeASound
