#pragma once

#include "RTMidi-Backend.h"
#include "../MIDI/MidiBackend.h"

#include <atomic>
#include <optional>

namespace MakeASound::RTMidi
{

void midiInputTrampoline(double timestamp,
                         std::vector<unsigned char>* message,
                         void* userData);

struct InputPort
{
    InputPort()
    {
        queue.reserve(2048);
        scratch.bytes.reserve(2048);
    }

    int portId {};
    OwningPointer<::RtMidiIn> rtIn;
    MidiInputCallback callback;

    // Handed to the callback by reference and refilled on the next message: a
    // MidiMessage built and destroyed per message is two heap operations on a thread
    // that is as real-time as the audio one.
    MidiMessage scratch;

    EA::Locks::PrimitiveSpinLock lock;
    Vector<MidiInputEvent> queue;
};

struct MidiManager : MidiBackend
{
    MidiManager();

    Error getLastError() const override;
    bool isAvailable() const override;

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
    void watch(::RtMidi* midi);
    InputPort* createInput(int portId, const MidiInputCallback& cb);

    // The platform's port numbers move under a hotplug and an id does not, so an id
    // is resolved against a fresh scan every time a port is opened. -1 once the
    // port the id names is gone.
    int resolveInput(int portId);
    int resolveOutput(int portId);

    // RtMidi reports either way: through the error callback once one is installed,
    // and by throwing out of a constructor, which is the only place there isn't one.
    template <class Fn>
    Error guard(Fn&& fn)
    {
        pendingError = Error::NoError;

        try
        {
            fn();
        }
        catch (const ::RtMidiError&)
        {
            lastError = Error::SYSTEM_ERROR;
            return lastError;
        }
        catch (...)
        {
            lastError = Error::UNKNOWN_ERROR;
            return lastError;
        }

        lastError = pendingError.load();
        return lastError;
    }

    OwningPointer<::RtMidiIn> inputEnumerator;
    OwningPointer<::RtMidiOut> outputEnumerator;
    OwningPointer<::RtMidiOut> output;
    EA::OwnedVector<InputPort> inputs;

    MidiPortRegistry inputRegistry;
    MidiPortRegistry outputRegistry;

    // Written by RtMidi's error callback, which can fire on its input thread.
    std::atomic<Error> pendingError {Error::NoError};
    Error lastError = Error::NoError;

    // Virtual inputs have no system port, so they get negative ids that cannot
    // collide with the registry slots getInputPorts() hands out.
    int nextVirtualPortId {-1};
};

} // namespace MakeASound::RTMidi
