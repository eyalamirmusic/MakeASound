#pragma once

#include "CoreMIDI-Backend.h"
#include "../MIDI/MidiBackendWithInputs.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace MakeASound::CoreMIDI
{

// One open input: the Core MIDI objects behind it, and the pipe its packets go
// through on Core MIDI's receive thread.
struct InputPort
{
    ~InputPort();

    // The mask is read once per packet list, then each packet parsed.
    void receive(const MIDIPacketList* list);

    MidiInputPipe pipe;

    // Exactly one of these: a system source the shared input port is connected
    // to, or a virtual destination of our own with its own receive block.
    MIDIEndpointRef source {};
    MIDIEndpointRef virtualDestination {};
    MIDIPortRef sharedInputPort {};
};

// Core MIDI delivers notifications on the run loop that was current when the
// process created its first MIDI client, and keeps that pin even after the
// thread is gone — a second manager built after the first had died would hear
// nothing. So the loop belongs to the process, not to a manager: one thread,
// started on first use and left running, on which a manager's client is created
// and disposed and its notify block and flag sweep run. Hotplug then works in a
// CLI too, with no run loop of the app's. Everything else Core MIDI owns — a
// port connect or disconnect, a virtual endpoint's creation or disposal — runs
// on whatever thread called for it.
//
// Where another library got its client in first the pin is theirs, and the
// notifications land on whatever loop that was; both paths still carry them.
//
// The loop also holds one client of its own for the life of the process. The iOS
// simulator's MIDIServer exits a few seconds after its last client is disposed,
// and the process's Core MIDI never reconnects: every later MIDIClientCreate
// fails for good. With the keep-alive client a manager can come and go any time.
class NotifyLoop
{
public:
    static NotifyLoop& get();

    // Runs `work` on the loop thread and waits for it, so a client is created and
    // disposed on the thread its notify block and sweep timer run on.
    void call(const std::function<void()>& work);

private:
    NotifyLoop();

    std::thread thread;
    std::atomic<CFRunLoopRef> loop {nullptr};
    MIDIClientRef keepAlive = 0;
};

struct MidiManager;

// Where another library pinned the notifications, the notify block runs on their
// run loop, unserialised against this manager's teardown. So the block holds one
// of these rather than a pointer: the destructor clears `owner` under the mutex,
// and a notification already inside has to finish before that returns.
struct NotifyGate
{
    std::mutex mutex;
    MidiManager* owner {};
};

struct MidiManager : MidiBackendWithInputs<InputPort>
{
    MidiManager();
    ~MidiManager() override;

    bool isAvailable() const override;

    Vector<MidiPortInfo> getInputPorts() override;
    Vector<MidiPortInfo> getOutputPorts() override;

    Error openInput(int portId, const MidiInputCallback& cb) override;
    std::optional<int> openVirtualInput(const std::string& name,
                                        const MidiInputCallback& cb) override;

    Error openOutput(int portId) override;
    Error openVirtualOutput(const std::string& name) override;
    void closeOutput() override;
    bool isOutputOpen() const override;

    Error sendMessage(const std::uint8_t* bytes, std::size_t size) override;

private:
    // All three run on NotifyLoop's thread, as does the timer start() adds: the
    // client is created and disposed there because that is where its notify
    // block lands, and the sweep that turns what the receive thread flagged into
    // notifications (publishInputFlags) is that timer.
    void start();
    void stop();
    void handleNotification(const MIDINotification* message);

    InputPort* createInput(int portId, const MidiInputCallback& cb);
    int resolveInput(int portId);
    int resolveOutput(int portId);

    using MidiBackendWithInputs::record;
    Error record(OSStatus status);

    MIDIClientRef client {};
    MIDIPortRef inputPort {};
    MIDIPortRef outputPort {};

    // The destination openOutput picked, or a virtual source of our own. Sending
    // to one is MIDISend and to the other MIDIReceived.
    MIDIEndpointRef destination {};
    MIDIEndpointRef virtualSource {};

    MidiPortRegistry inputRegistry;
    MidiPortRegistry outputRegistry;

    // Sweeps `inputFlags` on the loop thread, and keeps that loop from returning
    // for want of anything to do.
    CFRunLoopTimerRef sweepTimer {};

    std::shared_ptr<NotifyGate> gate;

    // Virtual inputs have no system port, so they get negative ids that cannot
    // collide with the registry slots getInputPorts() hands out.
    int nextVirtualPortId {-1};
};

} // namespace MakeASound::CoreMIDI
