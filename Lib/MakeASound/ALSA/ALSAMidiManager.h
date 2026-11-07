#pragma once

#include "ALSA-Backend.h"
#include "../MIDI/MidiBackendWithInputs.h"
#include "../Realtime/SPSCQueue.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>

namespace MakeASound::ALSA
{

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

// One open input: what it listens to, and the pipe what arrives goes through on
// the input thread.
struct InputPort
{
    ~InputPort();

    MidiInputPipe pipe;

    snd_seq_t* seq {};

    // One shape or the other: a subscription from `source` into the
    // `sharedInputPort` every opened input shares, or `seqPort`, a port of our
    // own that anything in the system can send to.
    Address source;
    int sharedInputPort {-1};
    int seqPort {-1};
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

    // The shared close, with the input thread parked around it.
    void closeInput(int portId) override;
    void closeAllInputs() override;

    Error openOutput(int portId) override;
    Error openVirtualOutput(const std::string& name) override;
    void closeOutput() override;
    bool isOutputOpen() const override;

    Error sendMessage(const std::uint8_t* bytes, std::size_t size) override;

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

    // The hotplug pair first, in the order it arrived, then the input flags.
    void publishFlags();

    void park();
    void resume();

    InputPort* createInput(int portId, const MidiInputCallback& cb);

    Address resolveInput(int portId);
    Address resolveOutput(int portId);
    int getSendPort() const;

    using MidiBackendWithInputs::record;
    Error record(int result);

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

    MidiPortRegistry inputRegistry;
    MidiPortRegistry outputRegistry;

    // The addresses behind the last enumeration, parallel to it, so an id
    // resolves without walking the sequencer a second time.
    Vector<Address> inputAddresses;
    Vector<Address> outputAddresses;

    static constexpr auto hotplugCapacity = 64;

    // What the input thread saw come and go, for the sweeper to raise: a queue
    // rather than a coalescing flag like the input ones, since for the hotplug
    // pair the order is the news.
    SPSCQueue<MidiNotification, hotplugCapacity> hotplug;

    WakeFd wake;
    std::thread inputThread;

    // Where notifyHost is called from, so the input thread never takes its lock.
    std::thread sweeper;

    std::atomic<bool> running {false};
    std::atomic<bool> parkRequested {false};
    std::atomic<bool> parked {false};

    std::mutex sweepMutex;
    std::condition_variable sweepSignal;

    // Where the queue's own clock reads zero, on the clock `epoch` is on.
    MidiTimePoint queueEpoch;

    // Virtual inputs have no system port, so they get negative ids that cannot
    // collide with the registry slots getInputPorts() hands out.
    int nextVirtualPortId {-1};
};

} // namespace MakeASound::ALSA
