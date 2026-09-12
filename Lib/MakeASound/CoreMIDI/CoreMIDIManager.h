#pragma once

#include "CoreMIDI-Backend.h"
#include "../MIDI/MidiBackend.h"
#include "../MIDI/MidiParser.h"
#include "../Realtime/SPSCQueue.h"

#include <atomic>
#include <functional>
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
// empties on the host's. Everything that thread touches is sized at open time.
struct InputPort
{
    static constexpr auto queueCapacity = 2048;

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

    SPSCQueue<MidiInputEvent, queueCapacity> queue;

private:
    void deliver(const MidiMessageView& message);
};

// Core MIDI delivers notifications on the run loop that was current when the
// process created its first MIDI client, and keeps that pin even after the
// thread is gone — a second manager built after the first had died would hear
// nothing. So the loop belongs to the process, not to a manager: one thread,
// started on first use and left running, on which every client is created, used
// and disposed. Hotplug then works in a CLI too, with no run loop of the app's.
//
// Where another library got its client in first the pin is theirs, and the
// notifications land on whatever loop that was; both paths still carry them.
class NotifyLoop
{
public:
    static NotifyLoop& get();

    // Runs `work` on the loop thread and waits for it, so nothing a manager does
    // to its Core MIDI objects races the notify block.
    void call(const std::function<void()>& work);

private:
    NotifyLoop();

    std::thread thread;
    std::atomic<CFRunLoopRef> loop {nullptr};
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
    // Both run on NotifyLoop's thread: everything Core MIDI owns is made and
    // unmade there, and the sweep that turns what the receive thread flagged into
    // notifications runs there too.
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

    MidiPortRegistry inputRegistry;
    MidiPortRegistry outputRegistry;

    SharedState shared;

    // Sweeps `shared` on the loop thread, and keeps that loop from returning for
    // want of anything to do.
    CFRunLoopTimerRef sweepTimer {};

    std::atomic<bool> shuttingDown {false};

    // MidiMessage::timestamp is seconds from here, on MidiManager::now()'s clock.
    MidiTimePoint epoch;

    Error lastError = Error::NoError;

    // Virtual inputs have no system port, so they get negative ids that cannot
    // collide with the registry slots getInputPorts() hands out.
    int nextVirtualPortId {-1};
};

} // namespace MakeASound::CoreMIDI
