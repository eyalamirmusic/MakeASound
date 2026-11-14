#pragma once

#include "WinMIDI-Backend.h"
#include "../MIDI/MidiBackendWithInputs.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace MakeASound::WinMIDI
{

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

// One open input: the WinMM handle and its buffer ring, and the pipe what
// arrives goes through on WinMM's callback thread.
struct InputPort
{
    static constexpr auto numSysExBuffers = 4;

    ~InputPort();

    // Opens and starts the device. The pipe is opened first, by the manager, so
    // it is ready before the first callback can land.
    Error open(int deviceNumber);

    // Both run on WinMM's callback thread.
    void handleShortMessage(DWORD_PTR packed, DWORD_PTR timestamp);
    void handleLongMessage(MIDIHDR* midiHeader, DWORD_PTR timestamp, bool failed);

    MidiInputPipe pipe;

    HMIDIIN handle {};

    // WinMM's own milliseconds are counted from here, which is steady_clock as of
    // midiInStart.
    MidiTimePoint startTime {};

private:
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
    // Enumerates, then turns what the callback thread flagged
    // (publishInputFlags) and what the enumeration moved (publishPortChanges)
    // into notifications. All of it runs on whichever host thread asked for the
    // ports: WinMM's callback may call almost nothing, drainMessages is the
    // audio thread's and notifyHost locks, and a timer thread of our own would
    // buy a host that never enumerates notifications it never asked for.
    Vector<MidiPortInfo> scan(Direction direction);
    void publishPortChanges(Vector<int>& known, const Vector<MidiPortInfo>& ports);

    int resolveInput(int portId);
    int resolveOutput(int portId);

    Error sendSysEx(const std::uint8_t* bytes, int size);

    using MidiBackendWithInputs::record;
    Error record(MMRESULT result);

    HMIDIOUT output {};

    // Staging for midiOutLongMsg, sized once at open so the send path never
    // reaches the allocator. It is the manager's SysEx ceiling going out, the
    // way maxSysExBytes is coming in.
    std::vector<char> sysExOut;

    MidiPortRegistry inputRegistry;
    MidiPortRegistry outputRegistry;

    // The ids the last scan saw, which is what the next one is diffed against.
    Vector<int> knownInputs;
    Vector<int> knownOutputs;
};

} // namespace MakeASound::WinMIDI
