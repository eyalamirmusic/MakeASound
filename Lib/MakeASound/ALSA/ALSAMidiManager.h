#pragma once

#include "ALSA-Backend.h"
#include "../MIDI/MidiBackend.h"
#include "../MIDI/MidiParser.h"
#include "../Realtime/SPSCQueue.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace MakeASound::ALSA
{

constexpr auto ignoreClockFlag = std::uint8_t {1};
constexpr auto ignoreActiveSenseFlag = std::uint8_t {2};

// The only place the input thread and the host's meet. notifyHost takes a lock,
// which that thread may not, so it leaves what it saw here and the sweeper turns
// it into notifications: a coalescing flag for the drops and overflows a stalled
// host could otherwise raise per message, a queue for the hotplug pair, where
// the order is the news. The mask goes the other way, read once per event so a
// setIgnoredTypes() reaches ports that are already open.
struct SharedState
{
    static constexpr auto hotplugCapacity = 64;

    std::atomic<bool> overflowed {false};
    std::atomic<bool> droppedSysEx {false};
    std::atomic<std::uint8_t> ignoredTypes {ignoreClockFlag | ignoreActiveSenseFlag};

    SPSCQueue<MidiNotification, hotplugCapacity> hotplug;
};

// The descriptor the poll loop watches alongside the sequencer's, so parking the
// input thread or shutting it down costs a write rather than the poll's timeout.
class WakeFd
{
public:
    WakeFd();
    ~WakeFd();

    WakeFd(const WakeFd&) = delete;
    WakeFd& operator=(const WakeFd&) = delete;

    int get() const noexcept { return fd; }

    void signal() noexcept;
    void clear() noexcept;

private:
    int fd {-1};
};

// One open input: what it listens to, the parser that turns what arrives into
// whole messages, and the two ways a message leaves - straight to a callback on
// the input thread, or into the queue drainMessages() empties on the host's.
// Everything that thread touches is sized when the port opens.
struct InputPort
{
    static constexpr auto queueCapacity = 2048;

    ~InputPort();

    void open(int bytesForSysEx, MidiTimePoint epochToUse, SharedState& state);
    void receive(Span<const std::uint8_t> bytes, MidiTimePoint timestamp);

    // Runs the SysEx timeout against a dump nothing is adding to any more, which
    // is what the input thread does with a poll that timed out.
    void tick(MidiTimePoint timestamp);

    int portId {};
    MidiInputCallback callback;

    snd_seq_t* seq {};

    // One shape or the other: a subscription from `source` into the
    // `sharedInputPort` every opened input shares, or `seqPort`, a port of our
    // own that anything in the system can send to.
    Address source;
    int sharedInputPort {-1};
    int seqPort {-1};

    MidiParser parser;
    std::vector<std::uint8_t> sysExStorage;
    MidiMessage scratch;
    MidiTimePoint epoch {};
    SharedState* shared {};

    SPSCQueue<MidiInputEvent, queueCapacity> queue;

private:
    void deliver(const MidiMessageView& message);
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
    // The open-port list belongs to the input thread while it runs, and a lock is
    // the one thing that thread may not take. So the host parks it instead - a
    // write to the wake-up descriptor, an acknowledgement, and the list is the
    // host's until the guard dies - for one relaxed load per wake-up.
    struct Parked
    {
        explicit Parked(MidiManager& owner);
        ~Parked();

        Parked(const Parked&) = delete;
        Parked& operator=(const Parked&) = delete;

        MidiManager& manager;
    };

    void start();
    void stop();

    // A sequencer port of ours. An input port asks the kernel to stamp what
    // lands on it from the queue's realtime clock, which is where
    // MidiInputEvent::arrival comes from.
    int createPort(const std::string& name, unsigned int caps, bool timestamped);
    void startQueue();

    void runInput();
    void processEvents();
    void handleEvent(const snd_seq_event_t& event);
    void tickPorts();
    InputPort* findPort(const snd_seq_event_t& event);

    void runSweeper();
    void publishFlags();

    void park();
    void resume();

    InputPort* createInput(int portId, const MidiInputCallback& cb);
    void removeInput(int portId);

    Address resolveInput(int portId);
    Address resolveOutput(int portId);
    int getSendPort() const;

    Error record(int result);
    Error record(Error error);

    snd_seq_t* seq {};
    int clientId {-1};

    // The port every subscribed input lands on, and the port every send leaves
    // by: one of each for the client, as the sequencer expects.
    int inputPort {-1};
    int outputPort {-1};
    int virtualOutputPort {-1};
    int queueId {-1};

    snd_midi_event_t* decoder {};
    snd_midi_event_t* encoder {};

    // Where openOutput subscribed, when it was a system port rather than a
    // virtual one of ours.
    Address destination;

    EA::OwnedVector<InputPort> inputs;

    MidiPortRegistry inputRegistry;
    MidiPortRegistry outputRegistry;

    // The addresses behind the last enumeration, parallel to it, so an id
    // resolves without walking the sequencer a second time.
    Vector<Address> inputAddresses;
    Vector<Address> outputAddresses;

    SharedState shared;

    WakeFd wake;
    std::thread inputThread;

    // Where notifyHost is called from, so the input thread never takes its lock.
    std::thread sweeper;

    std::atomic<bool> running {false};
    std::atomic<bool> parkRequested {false};
    std::atomic<bool> parked {false};

    std::mutex sweepMutex;
    std::condition_variable sweepSignal;

    // MidiMessage::timestamp is seconds from here, on MidiManager::now()'s clock.
    MidiTimePoint epoch;

    // Where the queue's own clock reads zero, on that same clock.
    MidiTimePoint queueEpoch;

    Error lastError = Error::NoError;

    // Virtual inputs have no system port, so they get negative ids that cannot
    // collide with the registry slots getInputPorts() hands out.
    int nextVirtualPortId {-1};
};

} // namespace MakeASound::ALSA
