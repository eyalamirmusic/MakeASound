#pragma once

#include "../MIDI/MidiBackend.h"

#include <atomic>
#include <optional>

namespace MakeASound::NullMIDI
{

// The backend for a platform with no MIDI API of ours - Android, Emscripten - and
// for a build that asks for none (MAKEASOUND_MIDI_BACKEND=Null). It is ALSA with
// no sequencer, on purpose: isAvailable() is false, every enumeration is empty,
// every open fails with SYSTEM_ERROR, and nothing else does anything. The façade
// stays constructible and usable, so audio-only code links and runs unchanged.
struct MidiManager : MidiBackend
{
    bool isAvailable() const override;
    Error getLastError() const override;

    Vector<MidiPortInfo> getInputPorts() override;
    Vector<MidiPortInfo> getOutputPorts() override;

    Error openInput(int portId, const MidiInputCallback& cb) override;
    std::optional<int> openVirtualInput(const std::string& name,
                                        const MidiInputCallback& cb) override;

    void closeInput(int portId) override;
    void closeAllInputs() override;
    bool isInputOpen(int portId) const override;
    Vector<int> getOpenInputPorts() const override;
    void drainMessages(Vector<MidiInputEvent>& out) override;

    Error openOutput(int portId) override;
    Error openVirtualOutput(const std::string& name) override;
    void closeOutput() override;
    bool isOutputOpen() const override;

    Error sendMessage(const std::uint8_t* bytes, std::size_t size) override;

private:
    Error record(Error error) noexcept;

    std::atomic<Error> lastError {Error::NoError};
};

} // namespace MakeASound::NullMIDI
