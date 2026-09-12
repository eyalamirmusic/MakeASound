#include "CoreMIDIManager.h"
#include "../MIDI/MIDI.h"

#include <mach/mach_time.h>

#include <algorithm>
#include <chrono>
#include <future>
#include <utility>

namespace MakeASound::CoreMIDI
{
namespace
{
// How often the run loop turns what the receive thread flagged into
// notifications. One sweep per interval, so a host that stopped draining costs a
// few notifications a second rather than one per dropped message.
constexpr auto flagSweepSeconds = 0.1;

// A name Core MIDI can hold, or null for one it cannot encode.
struct ScopedCFString
{
    explicit ScopedCFString(const std::string& text)
        : value(CFStringCreateWithCString(nullptr,
                                          text.c_str(),
                                          kCFStringEncodingUTF8))
    {
    }

    ~ScopedCFString()
    {
        if (value != nullptr)
            CFRelease(value);
    }

    ScopedCFString(const ScopedCFString&) = delete;
    ScopedCFString& operator=(const ScopedCFString&) = delete;

    CFStringRef value {};
};

// SysEx is the one thing convertMidi does not carry, and the queue tier only
// takes a dump that fits an Event. Anything longer belongs in callback mode.
std::optional<MIDI::Event> toEvent(Span<const std::uint8_t> bytes)
{
    if (bytes.empty())
        return std::nullopt;

    if (bytes[0] == 0xF0)
    {
        if (bytes.size() > MIDI::SysEx::maxBytes)
            return std::nullopt;

        return MIDI::Event::sysEx(bytes.data(), bytes.size());
    }

    return MIDI::convertMidi(bytes.data(), bytes.size());
}

// Takes the lock only if it was free. The audio thread skips a drain rather than
// waiting behind the open or close that is the only thing ever holding it.
struct ScopedTryLock
{
    explicit ScopedTryLock(EA::Locks::PrimitiveSpinLock& lockToUse)
        : lock(lockToUse)
        , held(lockToUse.tryLock())
    {
    }

    ~ScopedTryLock()
    {
        if (held)
            lock.unlock();
    }

    ScopedTryLock(const ScopedTryLock&) = delete;
    ScopedTryLock& operator=(const ScopedTryLock&) = delete;

    EA::Locks::PrimitiveSpinLock& lock;
    bool held {};
};

using ScopedLock = EA::Locks::ScopedSpinLock<EA::Locks::PrimitiveSpinLock>;
} // namespace

InputPort::~InputPort()
{
    // Core MIDI first, so nothing can be in a receive block for storage that is
    // about to go away with the rest of this object.
    if (virtualDestination != 0)
        MIDIEndpointDispose(virtualDestination);

    if (sharedInputPort != 0 && source != 0)
        MIDIPortDisconnectSource(sharedInputPort, source);
}

void InputPort::open(int bytesForSysEx, MidiTimePoint epochToUse, SharedState& state)
{
    shared = &state;
    epoch = epochToUse;

    sysExStorage.resize(static_cast<std::size_t>(bytesForSysEx));
    parser.setSysExBuffer(Span<std::uint8_t>(sysExStorage));

    // One mode's storage, never both. The callback's message never outgrows the
    // dump the parser can assemble, so reserving here is what keeps assign() off
    // the heap on the MIDI thread; queue mode has no use for it, and callback
    // mode none for the queue.
    if (callback)
        scratch.bytes.reserve(sysExStorage.size());
    else
        queue.create();
}

void InputPort::receive(const MIDIPacketList* list)
{
    auto flags = shared->ignoredTypes.load(std::memory_order_relaxed);
    parser.setIgnoredTypes((flags & ignoreClockFlag) != 0,
                           (flags & ignoreActiveSenseFlag) != 0);

    auto onMessage = [this](const MidiMessageView& message) { deliver(message); };

    auto onDropped = [this](int)
    { shared->droppedSysEx.store(true, std::memory_order_relaxed); };

    auto onPacket = [&](Span<const std::uint8_t> bytes, MidiTimePoint timestamp)
    { parser.feed(bytes, timestamp, onMessage, onDropped); };

    forEachPacket(list, onPacket);
}

void InputPort::deliver(const MidiMessageView& message)
{
    if (callback)
    {
        scratch.timestamp =
            std::chrono::duration<double>(message.timestamp - epoch).count();

        scratch.bytes.assign(message.bytes.begin(), message.bytes.end());
        callback(scratch);
        return;
    }

    auto typed = toEvent(message.bytes);

    if (!typed)
    {
        if (!message.bytes.empty() && message.bytes[0] == 0xF0)
            shared->droppedSysEx.store(true, std::memory_order_relaxed);

        return;
    }

    auto event = MidiInputEvent {portId, *typed, message.timestamp};

    if (!queue->push(event))
        shared->overflowed.store(true, std::memory_order_relaxed);
}

NotifyLoop& NotifyLoop::get()
{
    // Deliberately never destroyed: the loop has to outlive every client in the
    // process, static destruction order included.
    static auto* instance = new NotifyLoop {};
    return *instance;
}

NotifyLoop::NotifyLoop()
{
    auto ready = std::promise<void> {};
    auto started = ready.get_future();
    auto* signal = &ready;

    thread = std::thread(
        [this, signal]
        {
            loop.store(CFRunLoopGetCurrent());

            // A source with nothing to do: CFRunLoopRun returns at once on a loop
            // that has none, and every manager's timer comes and goes.
            auto context = CFRunLoopSourceContext {};
            context.perform = [](void*) {};

            auto* idle = CFRunLoopSourceCreate(nullptr, 0, &context);
            CFRunLoopAddSource(CFRunLoopGetCurrent(), idle, kCFRunLoopDefaultMode);

            signal->set_value();

            // Anything in the process can CFRunLoopStop this loop, and every
            // later call() would then wait on a loop that had returned.
            while (true)
                CFRunLoopRun();
        });

    started.wait();
    thread.detach();
}

void NotifyLoop::call(const std::function<void()>& work)
{
    auto* target = loop.load();

    if (target == nullptr || CFRunLoopGetCurrent() == target)
    {
        work();
        return;
    }

    auto done = std::promise<void> {};
    auto waiting = done.get_future();

    auto* task = &work;
    auto* signal = &done;

    CFRunLoopPerformBlock(target,
                          kCFRunLoopDefaultMode,
                          ^{
                              (*task)();
                              signal->set_value();
                          });

    CFRunLoopWakeUp(target);
    waiting.wait();
}

MidiManager::MidiManager()
    : gate(std::make_shared<NotifyGate>())
    , epoch(std::chrono::steady_clock::now())
{
    gate->owner = this;
    NotifyLoop::get().call([this] { start(); });
}

MidiManager::~MidiManager()
{
    // Closes the gate first, and waits for whatever was already through it: past
    // here the notify block has nothing left to call.
    {
        auto lock = std::lock_guard(gate->mutex);
        gate->owner = nullptr;
    }

    closeAllInputs();
    closeOutput();

    NotifyLoop::get().call([this] { stop(); });
}

void MidiManager::start()
{
    auto gateForBlock = gate;

    auto status = MIDIClientCreateWithBlock(
        CFSTR("MakeASound"),
        &client,
        ^(const MIDINotification* message)
        {
            auto lock = std::lock_guard(gateForBlock->mutex);

            if (gateForBlock->owner != nullptr)
                gateForBlock->owner->handleNotification(message);
        });

    if (status == noErr)
        status = MIDIInputPortCreateWithBlock(
            client,
            CFSTR("MakeASound Input"),
            &inputPort,
            ^(const MIDIPacketList* list, void* refCon)
            {
                if (refCon != nullptr)
                    static_cast<InputPort*>(refCon)->receive(list);
            });

    if (status == noErr)
        status =
            MIDIOutputPortCreate(client, CFSTR("MakeASound Output"), &outputPort);

    record(status);

    if (status != noErr)
        return;

    auto context = CFRunLoopTimerContext {0, this, nullptr, nullptr, nullptr};

    sweepTimer = CFRunLoopTimerCreate(
        nullptr,
        CFAbsoluteTimeGetCurrent() + flagSweepSeconds,
        flagSweepSeconds,
        0,
        0,
        [](CFRunLoopTimerRef, void* info)
        { static_cast<MidiManager*>(info)->publishFlags(); },
        &context);

    CFRunLoopAddTimer(CFRunLoopGetCurrent(), sweepTimer, kCFRunLoopDefaultMode);
}

void MidiManager::stop()
{
    if (sweepTimer != nullptr)
    {
        CFRunLoopRemoveTimer(CFRunLoopGetCurrent(),
                             sweepTimer,
                             kCFRunLoopDefaultMode);
        CFRelease(sweepTimer);
        sweepTimer = nullptr;
    }

    // Ports before the client: nothing may still be pointing at this manager by
    // the time the client that could call it is gone.
    if (inputPort != 0)
        MIDIPortDispose(inputPort);

    if (outputPort != 0)
        MIDIPortDispose(outputPort);

    if (client != 0)
        MIDIClientDispose(client);

    inputPort = 0;
    outputPort = 0;
    client = 0;
}

void MidiManager::handleNotification(const MIDINotification* message)
{
    if (message == nullptr)
        return;

    auto added = message->messageID == kMIDIMsgObjectAdded;

    // kMIDIMsgSetupChanged needs nothing: every enumeration rescans anyway.
    if (!added && message->messageID != kMIDIMsgObjectRemoved)
        return;

    const auto& change =
        *reinterpret_cast<const MIDIObjectAddRemoveNotification*>(message);

    if (change.childType != kMIDIObjectType_Source
        && change.childType != kMIDIObjectType_Destination)
        return;

    notifyHost(added ? MidiNotification::PortAdded : MidiNotification::PortRemoved);
}

void MidiManager::publishFlags()
{
    if (shared.overflowed.exchange(false))
        notifyHost(MidiNotification::QueueOverflow);

    if (shared.droppedSysEx.exchange(false))
        notifyHost(MidiNotification::SysExDropped);
}

Error MidiManager::record(OSStatus status)
{
    return record(getError(status));
}

Error MidiManager::record(Error error)
{
    lastError.store(error, std::memory_order_relaxed);
    return error;
}

Error MidiManager::getLastError() const
{
    return lastError.load(std::memory_order_relaxed);
}

bool MidiManager::isAvailable() const
{
    return client != 0;
}

Vector<MidiPortInfo> MidiManager::getInputPorts()
{
    if (client == 0)
        return {};

    return getPorts(Direction::Input, inputRegistry);
}

Vector<MidiPortInfo> MidiManager::getOutputPorts()
{
    if (client == 0)
        return {};

    return getPorts(Direction::Output, outputRegistry);
}

int MidiManager::resolveInput(int portId)
{
    getInputPorts();
    return inputRegistry.portNumberForId(portId);
}

int MidiManager::resolveOutput(int portId)
{
    getOutputPorts();
    return outputRegistry.portNumberForId(portId);
}

void MidiManager::setIgnoredTypes(bool clock, bool activeSense)
{
    MidiBackend::setIgnoredTypes(clock, activeSense);

    auto flags = static_cast<std::uint8_t>(
        (clock ? ignoreClockFlag : 0) | (activeSense ? ignoreActiveSenseFlag : 0));

    shared.ignoredTypes.store(flags, std::memory_order_relaxed);
}

InputPort* MidiManager::createInput(int portId, const MidiInputCallback& cb)
{
    // Built before the lock is taken: open() sizes the port's buffers, and the
    // drain has better things to do than wait behind that.
    auto port = EA::makeOwned<InputPort>();

    port->portId = portId;
    port->callback = cb;
    port->open(getMaxSysExBytes(), epoch, shared);

    auto* created = port.get();

    auto lock = ScopedLock(inputsLock);
    inputs.add(std::move(port));

    return created;
}

Error MidiManager::openInput(int portId, const MidiInputCallback& cb)
{
    // A virtual port's id, which no system port answers to. Rejected before the
    // close below, which would otherwise take that virtual port with it.
    if (portId < 0)
        return record(Error::INVALID_PARAMETER);

    closeInput(portId);

    if (client == 0 || inputPort == 0)
        return record(Error::SYSTEM_ERROR);

    auto endpoint = getEndpoint(Direction::Input, resolveInput(portId));

    if (endpoint == 0)
        return record(Error::INVALID_PARAMETER);

    auto* port = createInput(portId, cb);
    auto status = MIDIPortConnectSource(inputPort, endpoint, port);

    if (status != noErr)
    {
        closeInput(portId);
        return record(status);
    }

    port->source = endpoint;
    port->sharedInputPort = inputPort;

    return record(noErr);
}

std::optional<int> MidiManager::openVirtualInput(const std::string& name,
                                                 const MidiInputCallback& cb)
{
    if (client == 0)
    {
        record(Error::SYSTEM_ERROR);
        return std::nullopt;
    }

    auto cfName = ScopedCFString {name};

    if (cfName.value == nullptr)
    {
        record(Error::INVALID_PARAMETER);
        return std::nullopt;
    }

    auto portId = nextVirtualPortId--;
    auto* port = createInput(portId, cb);

    // The names invert here: a port we receive on is a Core MIDI destination,
    // which is why openVirtualOutput below is the one that makes a source.
    auto endpoint = MIDIEndpointRef {};
    auto status = MIDIDestinationCreateWithBlock(
        client,
        cfName.value,
        &endpoint,
        ^(const MIDIPacketList* list, void*) { port->receive(list); });

    if (status != noErr)
    {
        closeInput(portId);
        record(status);
        return std::nullopt;
    }

    port->virtualDestination = endpoint;
    record(noErr);

    return portId;
}

void MidiManager::closeInput(int portId)
{
    auto lock = ScopedLock(inputsLock);
    inputs.eraseIf([portId](auto& p) { return p->portId == portId; });
}

void MidiManager::closeAllInputs()
{
    auto lock = ScopedLock(inputsLock);
    inputs.clear();
}

bool MidiManager::isInputOpen(int portId) const
{
    for (auto& p: inputs)
        if (p->portId == portId)
            return true;

    return false;
}

Vector<int> MidiManager::getOpenInputPorts() const
{
    auto result = Vector<int> {};
    result.reserve(inputs.size());

    for (auto& p: inputs)
        result.add(p->portId);

    return result;
}

void MidiManager::drainMessages(Vector<MidiInputEvent>& out)
{
    // Contended only while a port is opening or closing, and this is the audio
    // thread: the events keep, so the block gives up its turn instead.
    auto lock = ScopedTryLock(inputsLock);

    if (!lock.held)
        return;

    auto event = MidiInputEvent {};

    for (auto& port: inputs)
    {
        if (port->callback)
            continue;

        while (port->queue->pop(event))
            out.add(event);
    }
}

Error MidiManager::openOutput(int portId)
{
    closeOutput();

    if (client == 0 || outputPort == 0)
        return record(Error::SYSTEM_ERROR);

    auto endpoint = getEndpoint(Direction::Output, resolveOutput(portId));

    if (endpoint == 0)
        return record(Error::INVALID_PARAMETER);

    destination = endpoint;

    return record(noErr);
}

Error MidiManager::openVirtualOutput(const std::string& name)
{
    closeOutput();

    if (client == 0)
        return record(Error::SYSTEM_ERROR);

    auto cfName = ScopedCFString {name};

    if (cfName.value == nullptr)
        return record(Error::INVALID_PARAMETER);

    auto status = MIDISourceCreate(client, cfName.value, &virtualSource);

    if (status != noErr)
    {
        virtualSource = 0;
        return record(status);
    }

    return record(noErr);
}

void MidiManager::closeOutput()
{
    if (virtualSource != 0)
    {
        MIDIEndpointDispose(virtualSource);
        virtualSource = 0;
    }

    destination = 0;
}

bool MidiManager::isOutputOpen() const
{
    return destination != 0 || virtualSource != 0;
}

Error MidiManager::sendMessage(const std::uint8_t* bytes, std::size_t size)
{
    // Sending to nothing used to succeed silently, which reads as a dead cable.
    if (!isOutputOpen())
        return record(Error::INVALID_USE);

    if (bytes == nullptr || size == 0)
        return record(Error::INVALID_PARAMETER);

    auto total = static_cast<int>(size);
    auto sent = 0;
    auto buffer = PacketListBuffer {};

    // Core MIDI schedules on the stamp and delivers anything already past at
    // once, so "now" is a real reading rather than the 0 that loses the message
    // its timestamp on the way through.
    auto timestamp = static_cast<MIDITimeStamp>(mach_absolute_time());

    // A dump longer than one list goes out as a run of stack-built ones, so
    // nothing here reaches the heap and no async send request outlives the call.
    while (sent < total)
    {
        auto taken = buildPacketList(buffer, bytes + sent, total - sent, timestamp);

        if (taken <= 0)
            return record(Error::MEMORY_ERROR);

        auto status = virtualSource != 0
                          ? MIDIReceived(virtualSource, buffer.get())
                          : MIDISend(outputPort, destination, buffer.get());

        if (status != noErr)
            return record(status);

        sent += taken;
    }

    return record(noErr);
}

} // namespace MakeASound::CoreMIDI
