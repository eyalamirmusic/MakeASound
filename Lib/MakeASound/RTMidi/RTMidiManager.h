#pragma once

#include "RTMidi-Backend.h"

#include <atomic>
#include <optional>

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

    Error getLastError() const;
    bool isAvailable() const;

    Vector<MidiPortInfo> getInputPorts();
    Vector<MidiPortInfo> getOutputPorts();

    Error openInput(int portId, const MidiInputCallback& cb);
    std::optional<int> openVirtualInput(const std::string& name,
                                        const MidiInputCallback& cb);
    void closeInput(int portId);
    void closeAllInputs();
    bool isInputOpen(int portId) const;
    Vector<int> getOpenInputPorts() const;
    void drainMessages(Vector<MidiInputEvent>& out);

    Error openOutput(int portId);
    Error openVirtualOutput(const std::string& name);
    void closeOutput();
    bool isOutputOpen() const;

    Error sendMessage(const std::uint8_t* bytes, std::size_t size);

private:
    void watch(::RtMidi* midi);
    InputPort* createInput(int portId, const MidiInputCallback& cb);

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

    // Written by RtMidi's error callback, which can fire on its input thread.
    std::atomic<Error> pendingError {Error::NoError};
    Error lastError = Error::NoError;

    // Virtual inputs have no system index, so they get negative ids that
    // cannot collide with the indices getInputPorts() returns.
    int nextVirtualPortId {-1};
};

} // namespace MakeASound::RTMidi
