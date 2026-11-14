#pragma once

#include "../Realtime/SpinLock.h"
#include "MidiBackend.h"
#include "MidiInputPipe.h"

#include <atomic>
#include <chrono>
#include <utility>

namespace MakeASound
{

// The half of a backend that is the same on every platform: the list of open
// inputs, the lock the drain shares with it, the flags their MIDI thread raises,
// and the last error. `Port` is the backend's own input, which holds a
// MidiInputPipe named `pipe` beside whatever native handles it needs; opening
// one, and everything native, stays the backend's.
template <class Port>
class MidiBackendWithInputs : public MidiBackend
{
public:
    MidiBackendWithInputs()
        : epoch(std::chrono::steady_clock::now())
    {
    }

    Error getLastError() const override
    {
        return lastError.load(std::memory_order_relaxed);
    }

    // A backend whose input thread has to be kept out of the list while it
    // changes overrides these two to do that around removeInput() and
    // removeAllInputs().
    void closeInput(int portId) override { removeInput(portId); }
    void closeAllInputs() override { removeAllInputs(); }

    bool isInputOpen(int portId) const override
    {
        for (auto& p: inputs)
            if (p->pipe.getPortId() == portId)
                return true;

        return false;
    }

    Vector<int> getOpenInputPorts() const override
    {
        auto result = Vector<int> {};
        result.reserve(inputs.size());

        for (auto& p: inputs)
            result.add(p->pipe.getPortId());

        return result;
    }

    void drainMessages(Vector<MidiInputEvent>& out) override
    {
        // Contended only while a port is opening or closing, and this is the
        // audio thread: the events keep, so the block gives up its turn instead.
        auto lock = ScopedTryLock(inputsLock);

        if (!lock.held)
            return;

        for (auto& port: inputs)
            port->pipe.drainInto(out);
    }

    void setIgnoredTypes(bool clock, bool activeSense) override
    {
        MidiBackend::setIgnoredTypes(clock, activeSense);
        inputFlags.setIgnoredTypes(clock, activeSense);
    }

protected:
    // A port with its pipe opened, not yet in the list: built before the lock is
    // taken, since sizing the buffers allocates and the drain has better things
    // to do than wait behind that.
    OwningPointer<Port> makeInput(int portId, const MidiInputCallback& cb)
    {
        auto port = EA::makeOwned<Port>();
        port->pipe.open(portId, cb, getMaxSysExBytes(), epoch, inputFlags);

        return port;
    }

    Port* addInput(OwningPointer<Port> port)
    {
        auto* added = port.get();

        auto lock = ScopedSpinLock(inputsLock);
        inputs.add(std::move(port));

        return added;
    }

    // A port's destructor is where its native side lets go, so it runs under
    // the lock, before the drain can see the list again.
    void removeInput(int portId)
    {
        auto lock = ScopedSpinLock(inputsLock);
        inputs.eraseIf([portId](auto& p) { return p->pipe.getPortId() == portId; });
    }

    void removeAllInputs()
    {
        auto lock = ScopedSpinLock(inputsLock);
        inputs.clear();
    }

    // Turns what the MIDI thread flagged into notifications, on whichever thread
    // the backend sweeps from.
    void publishInputFlags()
    {
        if (inputFlags.overflowed.exchange(false))
            notifyHost(MidiNotification::QueueOverflow);

        if (inputFlags.droppedSysEx.exchange(false))
            notifyHost(MidiNotification::SysExDropped);
    }

    Error record(Error error) noexcept
    {
        lastError.store(error, std::memory_order_relaxed);
        return error;
    }

    EA::OwnedVector<Port> inputs;

    // Guards `inputs` against the thread draining it. An open or close holds it;
    // drainMessages() only ever tries for it, so the audio thread never waits.
    EA::Locks::PrimitiveSpinLock inputsLock;

    MidiInputFlags inputFlags;

    // MidiMessage::timestamp is seconds from here, on MidiManager::now()'s clock.
    MidiTimePoint epoch;

private:
    // sendMessage() is callable from an audio thread and getLastError() from the
    // host's, so this is read and written across threads.
    std::atomic<Error> lastError {Error::NoError};
};

} // namespace MakeASound
