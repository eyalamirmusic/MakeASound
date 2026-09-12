#pragma once

#include "WinMIDI-Backend.h"
#include "../MIDI/MidiBackend.h"
#include "../MIDI/MidiParser.h"
#include "../Realtime/SPSCQueue.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace MakeASound::WinMIDI
{

constexpr auto ignoreClockFlag = std::uint8_t {1};
constexpr auto ignoreActiveSenseFlag = std::uint8_t {2};

// The only place WinMM's callback thread and the host's meet. notifyHost takes a
// mutex and WinMM allows almost nothing to be called from inside its callback,
// so the callback raises a flag and the enumeration sweeps it; the mask goes the
// other way, read once per callback so a setIgnoredTypes() reaches ports that
// are already open.
struct SharedState
{
    std::atomic<bool> overflowed {false};
    std::atomic<bool> droppedSysEx {false};
    std::atomic<std::uint8_t> ignoredTypes {ignoreClockFlag | ignoreActiveSenseFlag};
};

// One slot of an input's SysEx ring. WinMM fills the storage, hands the header
// back through MIM_LONGDATA, and the callback re-arms it on the spot — a dump
// longer than one slot simply spills into the next, so the parser sees it whole.
struct SysExBuffer
{
    static constexpr auto bytes = 1024;

    MIDIHDR header {};
    std::array<char, bytes> storage {};
    bool prepared {false};
};

// One open input: the WinMM handle and its buffer ring, the parser that turns
// the bytes into whole messages, and the two ways a message leaves — straight to
// a callback on WinMM's thread, or into the queue drainMessages() empties on the
// host's. Everything that thread touches is sized at open time.
struct InputPort
{
    static constexpr auto queueCapacity = 2048;
    static constexpr auto numSysExBuffers = 4;

    ~InputPort();

    Error open(int deviceNumber,
               int bytesForSysEx,
               MidiTimePoint epochToUse,
               SharedState& state);

    // Both run on WinMM's callback thread.
    void handleShortMessage(DWORD_PTR packed, DWORD_PTR timestamp);
    void handleLongMessage(MIDIHDR* midiHeader, DWORD_PTR timestamp, bool failed);

    int portId {};
    MidiInputCallback callback;

    HMIDIIN handle {};

    MidiParser parser;
    std::vector<std::uint8_t> sysExStorage;
    MidiMessage scratch;

    // MidiMessage::timestamp is seconds from here; WinMM's own milliseconds are
    // counted from startTime, which is steady_clock as of midiInStart.
    MidiTimePoint epoch {};
    MidiTimePoint startTime {};
    SharedState* shared {};

    SPSCQueue<MidiInputEvent, queueCapacity> queue;

private:
    void feed(const std::uint8_t* bytes, int size, MidiTimePoint timestamp);
    void deliver(const MidiMessageView& message);

    // True once every prepared header is back from the driver.
    bool unprepareBuffers();
    void close();

    std::array<SysExBuffer, numSysExBuffers> buffers {};

    // Read by the callback so a buffer midiInReset hands back on the way out is
    // not re-armed into a port that is going away.
    std::atomic<bool> closing {false};
    bool running {false};
};

// WinMM has no virtual ports, no hotplug notification and no loop thread of its
// own: openVirtual* refuse, and PortAdded/PortRemoved are diffed out of the
// enumeration, which is what a UI polling the list would have seen anyway.
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
    Vector<MidiPortInfo> scan(Direction direction);

    // Turns what the callback thread flagged, and what the enumeration moved,
    // into notifications. Both run on whichever host thread asked for the ports:
    // WinMM's callback may call almost nothing, drainMessages is the audio
    // thread's and notifyHost locks, and a timer thread of our own would buy a
    // host that never enumerates notifications it never asked for.
    void publishFlags();
    void publishPortChanges(Vector<int>& known, const Vector<MidiPortInfo>& ports);

    int resolveInput(int portId);
    int resolveOutput(int portId);

    Error sendSysEx(const std::uint8_t* bytes, int size);

    Error record(MMRESULT result);
    Error record(Error error);

    HMIDIOUT output {};

    // Staging for midiOutLongMsg, sized once at open so the send path never
    // reaches the allocator. It is the manager's SysEx ceiling going out, the
    // way maxSysExBytes is coming in.
    std::vector<char> sysExOut;

    SharedState shared;

    EA::OwnedVector<InputPort> inputs;

    MidiPortRegistry inputRegistry;
    MidiPortRegistry outputRegistry;

    // The ids the last scan saw, which is what the next one is diffed against.
    Vector<int> knownInputs;
    Vector<int> knownOutputs;

    MidiTimePoint epoch;

    Error lastError = Error::NoError;
};

} // namespace MakeASound::WinMIDI
