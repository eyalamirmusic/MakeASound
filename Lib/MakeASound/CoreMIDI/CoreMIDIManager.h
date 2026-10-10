#pragma once

#include "CoreMIDI-Backend.h"
#include "../MIDI/MidiBackend.h"
#include "../MIDI/MidiParser.h"
#include "../Realtime/SPSCQueue.h"
#include "../Realtime/SpinLock.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace MakeASound::CoreMIDI
{

constexpr auto ignoreClockFlag = std::uint8_t {1};
constexpr auto ignoreActiveSenseFlag = std::uint8_t {2};

// The only place Core MIDI's receive thread and the host's meet. notifyHost takes
// a mutex, so the receive thread raises a flag instead and the backend's run loop
// turns it into a notification; the mask goes the other way, read once per packet
// list so a setIgnoredTypes() reaches ports that are already open.
struct SharedState
{
    std::atomic<bool> overflowed {false};
    std::atomic<bool> droppedSysEx {false};
    std::atomic<std::uint8_t> ignoredTypes {ignoreClockFlag | ignoreActiveSenseFlag};
};

// One open input: the Core MIDI objects behind it, the parser that turns its
// packets into whole messages, and the two ways a message leaves — straight to a
// callback on Core MIDI's receive thread, or into the queue drainMessages()
// empties on the host's. Everything that thread touches is sized at open time,
// and only for the mode the port is in: queue mode gets the queue, callback mode
// a scratch message as large as the SysEx cap, and neither pays for the other.
struct InputPort
{
    static constexpr auto queueCapacity = 2048;

    using Queue = SPSCQueue<MidiInputEvent, queueCapacity>;

    ~InputPort();

    void open(int bytesForSysEx, MidiTimePoint epochToUse, SharedState& state);
    void receive(const MIDIPacketList* list);

    int portId {};
    MidiInputCallback callback;

    // Exactly one of these: a system source the shared input port is connected
    // to, or a virtual destination of our own with its own receive block.
    MIDIEndpointRef source {};
    MIDIEndpointRef virtualDestination {};
    MIDIPortRef sharedInputPort {};

    MidiParser parser;
    std::vector<std::uint8_t> sysExStorage;
    MidiMessage scratch;
    MidiTimePoint epoch {};
    SharedState* shared {};

    // Queue mode only; null where `callback` took the other path.
    OwningPointer<Queue> queue;

private:
    void deliver(const MidiMessageView& message);
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

struct MidiManager : MidiBackend
{
    MidiManager();
    ~MidiManager() override;

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

    void setIgnoredTypes(bool clock, bool activeSense) override;

private:
    // All four run on NotifyLoop's thread: the client is created and disposed
    // there because that is where its notify block lands, and the sweep that
    // turns what the receive thread flagged into notifications is its timer.
    void start();
    void stop();
    void handleNotification(const MIDINotification* message);
    void publishFlags();

    InputPort* createInput(int portId, const MidiInputCallback& cb);
    int resolveInput(int portId);
    int resolveOutput(int portId);

    Error record(OSStatus status);
    Error record(Error error);

    MIDIClientRef client {};
    MIDIPortRef inputPort {};
    MIDIPortRef outputPort {};

    // The destination openOutput picked, or a virtual source of our own. Sending
    // to one is MIDISend and to the other MIDIReceived.
    MIDIEndpointRef destination {};
    MIDIEndpointRef virtualSource {};

    EA::OwnedVector<InputPort> inputs;

    // Guards `inputs` against the thread draining it. An open or close holds it;
    // drainMessages() only ever tries for it, so the audio thread never waits.
    EA::Locks::PrimitiveSpinLock inputsLock;

    MidiPortRegistry inputRegistry;
    MidiPortRegistry outputRegistry;

    SharedState shared;

    // Sweeps `shared` on the loop thread, and keeps that loop from returning for
    // want of anything to do.
    CFRunLoopTimerRef sweepTimer {};

    std::shared_ptr<NotifyGate> gate;

    // MidiMessage::timestamp is seconds from here, on MidiManager::now()'s clock.
    MidiTimePoint epoch;

    // sendMessage() is callable from an audio thread and getLastError() from the
    // host's, so this is read and written across threads.
    std::atomic<Error> lastError {Error::NoError};

    // Virtual inputs have no system port, so they get negative ids that cannot
    // collide with the registry slots getInputPorts() hands out.
    int nextVirtualPortId {-1};
};

} // namespace MakeASound::CoreMIDI
