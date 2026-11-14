#include "ALSAMidiManager.h"

#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <utility>

namespace MakeASound::ALSA
{
namespace
{
// How long the input thread waits for something to happen before it looks at the
// clock. The SysEx timeout is measured on that thread, so a dump left hanging by
// a pulled cable is abandoned within one of these.
constexpr auto pollTimeoutMs = 100;

// How often the sweeper turns what the input thread left behind into
// notifications. One sweep per interval, so a host that stopped draining costs a
// few notifications a second rather than one per dropped message.
constexpr auto sweepInterval = std::chrono::milliseconds {100};

// How long the host waits for the input thread to answer a park. It answers
// within a poll's return; this is only here so a thread that died cannot take
// the thread that asked with it.
constexpr auto parkTimeout = std::chrono::milliseconds {2000};
constexpr auto parkPollInterval = std::chrono::microseconds {200};

// Room for a large dump to arrive as one event rather than a run of them. The
// library allocates it once, here, rather than on the input thread.
constexpr auto inputBufferBytes = 128 * 1024;

// All the encoder ever sees is a short message: a dump goes out pointing at the
// caller's own bytes instead.
constexpr auto encoderBufferBytes = 256;

// alsa-lib writes its own diagnostics to stderr through a process-wide hook. A
// machine with no sequencer is an ordinary state here, not something to print on
// a host's terminal, so the hook is silenced for as long as we are the one
// calling.
struct ScopedQuietAlsa
{
    ScopedQuietAlsa() { snd_lib_error_set_handler(&ignore); }
    ~ScopedQuietAlsa() { snd_lib_error_set_handler(nullptr); }

    ScopedQuietAlsa(const ScopedQuietAlsa&) = delete;
    ScopedQuietAlsa& operator=(const ScopedQuietAlsa&) = delete;

    static void ignore(const char*, int, const char*, int, const char*, ...) {}
};
} // namespace

WakeFd::WakeFd()
    : fd(::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK))
{
}

WakeFd::~WakeFd()
{
    if (fd >= 0)
        ::close(fd);
}

void WakeFd::signal() noexcept
{
    if (fd < 0)
        return;

    auto one = std::uint64_t {1};
    auto written = ::write(fd, &one, sizeof(one));

    (void) written;
}

void WakeFd::clear() noexcept
{
    if (fd < 0)
        return;

    auto value = std::uint64_t {};
    auto taken = ::read(fd, &value, sizeof(value));

    (void) taken;
}

InputPort::~InputPort()
{
    // The input thread is parked for as long as this runs, so unsubscribing is
    // all there is to do: nothing can be inside the pipe's feed() for storage
    // that is about to go away with the rest of this object.
    if (seq == nullptr)
        return;

    if (seqPort >= 0)
        snd_seq_delete_port(seq, seqPort);
    else if (source.isValid() && sharedInputPort >= 0)
        snd_seq_disconnect_from(seq, sharedInputPort, source.client, source.port);
}

MidiManager::Parked::Parked(MidiManager& owner)
    : manager(owner)
{
    manager.park();
}

MidiManager::Parked::~Parked()
{
    manager.resume();
}

MidiManager::MidiManager()
    : queueEpoch(epoch)
{
    start();
}

MidiManager::~MidiManager()
{
    closeAllInputs();
    closeOutput();
    stop();
}

void MidiManager::start()
{
    auto quiet = ScopedQuietAlsa {};

    // One client for the process, speaking both ways: a subscription is how a
    // port is opened here, and both directions go through the same handle.
    auto result = snd_seq_open(&seq, "default", SND_SEQ_OPEN_DUPLEX, 0);

    if (result < 0)
    {
        // A machine with no sequencer - a container with no /dev/snd/seq, a
        // kernel with no snd-seq - is an ordinary state, reported rather than
        // raised. isAvailable() is false and everything else is a no-op.
        seq = nullptr;
        record(result);
        return;
    }

    snd_seq_set_client_name(seq, "MakeASound");
    clientId = snd_seq_client_id(seq);

    snd_seq_set_input_buffer_size(seq, inputBufferBytes);

    result = snd_midi_event_new(0, &decoder);

    if (result >= 0)
        result = snd_midi_event_new(encoderBufferBytes, &encoder);

    if (result < 0)
    {
        record(result);
        return;
    }

    // Every message keeps its own status byte, so what the parser sees is the
    // wire and not a stream leaning on a status it was never handed.
    snd_midi_event_no_status(decoder, 1);

    startQueue();

    inputPort = createPort("MakeASound Input",
                           SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
                           true);

    if (inputPort < 0)
    {
        record(inputPort);
        return;
    }

    outputPort = createPort("MakeASound Output",
                            SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ,
                            false);

    if (outputPort < 0)
    {
        record(outputPort);
        return;
    }

    // Hotplug is a subscription like any other: the system client announces
    // every port that comes and goes on this one.
    snd_seq_connect_from(
        seq, inputPort, SND_SEQ_CLIENT_SYSTEM, SND_SEQ_PORT_SYSTEM_ANNOUNCE);

    running.store(true);

    inputThread = std::thread([this] { runInput(); });
    sweeper = std::thread([this] { runSweeper(); });

    record(0);
}

void MidiManager::stop()
{
    running.store(false);
    wake.signal();
    sweepSignal.notify_all();

    if (inputThread.joinable())
        inputThread.join();

    if (sweeper.joinable())
        sweeper.join();

    if (seq != nullptr)
    {
        if (queueId >= 0)
        {
            snd_seq_stop_queue(seq, queueId, nullptr);
            snd_seq_drain_output(seq);
            snd_seq_free_queue(seq, queueId);
            queueId = -1;
        }

        if (inputPort >= 0)
            snd_seq_delete_port(seq, inputPort);

        if (outputPort >= 0)
            snd_seq_delete_port(seq, outputPort);

        snd_seq_close(seq);
        seq = nullptr;
    }

    if (decoder != nullptr)
    {
        snd_midi_event_free(decoder);
        decoder = nullptr;
    }

    if (encoder != nullptr)
    {
        snd_midi_event_free(encoder);
        encoder = nullptr;
    }

    inputPort = -1;
    outputPort = -1;
}

void MidiManager::startQueue()
{
    queueId = snd_seq_alloc_named_queue(seq, "MakeASound");

    if (queueId < 0)
    {
        // Without a queue the kernel stamps nothing and arrivals fall back to
        // the moment they are read.
        queueId = -1;
        return;
    }

    snd_seq_start_queue(seq, queueId, nullptr);
    snd_seq_drain_output(seq);

    snd_seq_queue_status_t* status = nullptr;
    snd_seq_queue_status_alloca(&status);

    auto now = std::chrono::steady_clock::now();

    if (snd_seq_get_queue_status(seq, queueId, status) < 0)
        return;

    const auto* time = snd_seq_queue_status_get_real_time(status);

    if (time == nullptr)
        return;

    auto elapsed = std::chrono::seconds {time->tv_sec}
                   + std::chrono::nanoseconds {time->tv_nsec};

    // Where the queue's clock read zero, taken once: both it and steady_clock
    // are the machine's monotonic one, so the pair never drifts apart.
    queueEpoch = now - std::chrono::duration_cast<MidiTimePoint::duration>(elapsed);
}

int MidiManager::createPort(const std::string& name,
                            unsigned int caps,
                            bool timestamped)
{
    snd_seq_port_info_t* info = nullptr;
    snd_seq_port_info_alloca(&info);

    snd_seq_port_info_set_name(info, name.c_str());
    snd_seq_port_info_set_capability(info, caps);
    snd_seq_port_info_set_type(
        info, SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
    snd_seq_port_info_set_midi_channels(info, 16);
    snd_seq_port_info_set_port_specified(info, 0);

    if (timestamped && queueId >= 0)
    {
        snd_seq_port_info_set_timestamping(info, 1);
        snd_seq_port_info_set_timestamp_real(info, 1);
        snd_seq_port_info_set_timestamp_queue(info, queueId);
    }

    auto result = snd_seq_create_port(seq, info);

    if (result < 0)
        return result;

    return snd_seq_port_info_get_port(info);
}

void MidiManager::runInput()
{
    auto descriptors = std::array<pollfd, 8> {};

    auto sequencerCount = snd_seq_poll_descriptors_count(seq, POLLIN);
    sequencerCount =
        std::clamp(sequencerCount, 0, static_cast<int>(descriptors.size()) - 1);

    snd_seq_poll_descriptors(
        seq, descriptors.data(), static_cast<unsigned int>(sequencerCount), POLLIN);

    descriptors[sequencerCount].fd = wake.get();
    descriptors[sequencerCount].events = POLLIN;

    auto count = static_cast<nfds_t>(sequencerCount + 1);

    while (running.load(std::memory_order_relaxed))
    {
        if (parkRequested.load(std::memory_order_acquire))
        {
            parked.store(true, std::memory_order_release);

            while (parkRequested.load(std::memory_order_acquire))
                std::this_thread::sleep_for(parkPollInterval);

            parked.store(false, std::memory_order_release);
            continue;
        }

        auto ready = ::poll(descriptors.data(), count, pollTimeoutMs);

        if (ready < 0 && errno != EINTR)
            break;

        if (descriptors[sequencerCount].revents != 0)
            wake.clear();

        processEvents();
        tickPorts();
    }
}

void MidiManager::processEvents()
{
    snd_seq_event_t* event = nullptr;

    // Asking what is pending fetches without blocking, which is what keeps this
    // thread out of a read it would have to be woken from.
    while (snd_seq_event_input_pending(seq, 1) > 0)
    {
        if (snd_seq_event_input(seq, &event) < 0 || event == nullptr)
            break;

        handleEvent(*event);
    }
}

void MidiManager::handleEvent(const snd_seq_event_t& event)
{
    if (isPortAdded(event))
    {
        hotplug.push(MidiNotification::PortAdded);
        return;
    }

    if (isPortRemoved(event))
    {
        hotplug.push(MidiNotification::PortRemoved);
        return;
    }

    auto* port = findPort(event);

    if (port == nullptr)
        return;

    auto storage = std::array<std::uint8_t, maxShortMessageBytes> {};
    auto bytes = getEventBytes(decoder, event, Span<std::uint8_t>(storage));

    if (bytes.empty())
        return;

    port->pipe.feed(bytes, toTimePoint(event, queueEpoch));
}

InputPort* MidiManager::findPort(const snd_seq_event_t& event)
{
    for (auto& port: inputs)
    {
        // A port of our own is told by where the event landed; a subscription
        // shares one port with every other, so it is told by where it came from.
        if (port->seqPort >= 0)
        {
            if (port->seqPort == event.dest.port)
                return port.get();

            continue;
        }

        if (port->source.client == event.source.client
            && port->source.port == event.source.port)
            return port.get();
    }

    return nullptr;
}

void MidiManager::tickPorts()
{
    auto now = std::chrono::steady_clock::now();

    // Feeding nothing runs the SysEx timeout against a dump nothing is adding
    // to any more, which is what a poll that timed out is for.
    for (auto& port: inputs)
        port->pipe.feed({}, now);
}

void MidiManager::runSweeper()
{
    while (running.load())
    {
        {
            auto lock = std::unique_lock(sweepMutex);
            sweepSignal.wait_for(
                lock, sweepInterval, [this] { return !running.load(); });
        }

        publishFlags();
    }
}

void MidiManager::publishFlags()
{
    auto notification = MidiNotification {};

    while (hotplug.pop(notification))
        notifyHost(notification);

    publishInputFlags();
}

void MidiManager::park()
{
    // A host that opens or closes a port from inside its own MIDI callback is
    // already on the thread it would otherwise be waiting for.
    if (!running.load() || std::this_thread::get_id() == inputThread.get_id())
        return;

    parkRequested.store(true, std::memory_order_release);
    wake.signal();

    auto deadline = std::chrono::steady_clock::now() + parkTimeout;

    while (!parked.load(std::memory_order_acquire))
    {
        if (!running.load() || std::chrono::steady_clock::now() > deadline)
            return;

        std::this_thread::sleep_for(parkPollInterval);
    }
}

void MidiManager::resume()
{
    parkRequested.store(false, std::memory_order_release);
}

Error MidiManager::record(int result)
{
    return record(getError(result));
}

bool MidiManager::isAvailable() const
{
    return seq != nullptr && inputPort >= 0 && outputPort >= 0;
}

Vector<MidiPortInfo> MidiManager::getInputPorts()
{
    auto filter = PortFilter {clientId, inputPort, outputPort};

    return getPorts(seq, Direction::Input, filter, inputRegistry, inputAddresses);
}

Vector<MidiPortInfo> MidiManager::getOutputPorts()
{
    auto filter = PortFilter {clientId, inputPort, outputPort};

    return getPorts(seq, Direction::Output, filter, outputRegistry, outputAddresses);
}

Address MidiManager::resolveInput(int portId)
{
    getInputPorts();

    auto index = inputRegistry.portNumberForId(portId);

    if (index < 0 || index >= inputAddresses.size())
        return {};

    return inputAddresses.get(index);
}

Address MidiManager::resolveOutput(int portId)
{
    getOutputPorts();

    auto index = outputRegistry.portNumberForId(portId);

    if (index < 0 || index >= outputAddresses.size())
        return {};

    return outputAddresses.get(index);
}

InputPort* MidiManager::createInput(int portId, const MidiInputCallback& cb)
{
    // Everything the destructor needs is set before the port reaches the list.
    auto port = makeInput(portId, cb);

    port->seq = seq;
    port->sharedInputPort = inputPort;

    return addInput(std::move(port));
}

Error MidiManager::openInput(int portId, const MidiInputCallback& cb)
{
    // A virtual port's id, which no system port answers to. Rejected before the
    // close below, which would otherwise take that virtual port with it.
    if (portId < 0)
        return record(Error::INVALID_PARAMETER);

    closeInput(portId);

    if (!isAvailable())
        return record(Error::SYSTEM_ERROR);

    auto address = resolveInput(portId);

    if (!address.isValid())
        return record(Error::INVALID_PARAMETER);

    auto parking = Parked {*this};
    auto* port = createInput(portId, cb);

    // Opening a port here is subscribing to it: what it reads reaches everyone
    // who asked, us included, until the subscription goes away.
    auto result = snd_seq_connect_from(seq, inputPort, address.client, address.port);

    if (result < 0)
    {
        removeInput(portId);
        return record(result);
    }

    port->source = address;

    return record(0);
}

std::optional<int> MidiManager::openVirtualInput(const std::string& name,
                                                 const MidiInputCallback& cb)
{
    if (!isAvailable())
    {
        record(Error::SYSTEM_ERROR);
        return std::nullopt;
    }

    auto parking = Parked {*this};

    // A virtual input is a port of ours that anything in the system may send to,
    // which is what makes it the loopback a machine with no hardware still has.
    auto seqPort =
        createPort(name, SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE, true);

    if (seqPort < 0)
    {
        record(seqPort);
        return std::nullopt;
    }

    auto portId = nextVirtualPortId--;
    auto* port = createInput(portId, cb);

    port->seqPort = seqPort;
    record(0);

    return portId;
}

// Parking keeps the input thread out of the list; the lock removeInput takes is
// for the drain, which parking does nothing for.
void MidiManager::closeInput(int portId)
{
    auto parking = Parked {*this};
    removeInput(portId);
}

void MidiManager::closeAllInputs()
{
    auto parking = Parked {*this};
    removeAllInputs();
}

Error MidiManager::openOutput(int portId)
{
    closeOutput();

    if (!isAvailable())
        return record(Error::SYSTEM_ERROR);

    auto address = resolveOutput(portId);

    if (!address.isValid())
        return record(Error::INVALID_PARAMETER);

    auto result = snd_seq_connect_to(seq, outputPort, address.client, address.port);

    if (result < 0)
        return record(result);

    destination = address;

    return record(0);
}

Error MidiManager::openVirtualOutput(const std::string& name)
{
    closeOutput();

    if (!isAvailable())
        return record(Error::SYSTEM_ERROR);

    auto seqPort =
        createPort(name, SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ, false);

    if (seqPort < 0)
        return record(seqPort);

    virtualOutputPort = seqPort;

    return record(0);
}

void MidiManager::closeOutput()
{
    if (seq == nullptr)
        return;

    if (destination.isValid())
    {
        snd_seq_disconnect_to(seq, outputPort, destination.client, destination.port);

        destination = {};
    }

    if (virtualOutputPort >= 0)
    {
        snd_seq_delete_port(seq, virtualOutputPort);
        virtualOutputPort = -1;
    }
}

bool MidiManager::isOutputOpen() const
{
    return destination.isValid() || virtualOutputPort >= 0;
}

int MidiManager::getSendPort() const
{
    return virtualOutputPort >= 0 ? virtualOutputPort : outputPort;
}

Error MidiManager::sendMessage(const std::uint8_t* bytes, std::size_t size)
{
    // Sending to nothing used to succeed silently, which reads as a dead cable.
    if (!isOutputOpen())
        return record(Error::INVALID_USE);

    if (bytes == nullptr || size == 0)
        return record(Error::INVALID_PARAMETER);

    auto message = Span<const std::uint8_t>(bytes, static_cast<int>(size));
    auto sent = 0;

    // A dump of any size goes out as a run of stack-built events, so nothing
    // here reaches the heap and nothing outlives the call.
    while (sent < message.size())
    {
        auto event = snd_seq_event_t {};
        auto taken = buildEvent(encoder, message, sent, event);

        if (taken <= 0)
            return record(Error::INVALID_PARAMETER);

        snd_seq_ev_set_source(&event, getSendPort());
        snd_seq_ev_set_subs(&event);
        snd_seq_ev_set_direct(&event);

        // Through the library's own buffer rather than output_direct, which
        // grows a scratch buffer of its own the first time it is handed a dump.
        auto result = snd_seq_event_output(seq, &event);

        if (result < 0)
            return record(result);

        sent += taken;
    }

    // Nothing is on its way until this: the send is synchronous, as the façade
    // says, and the events reach the kernel in the order they were built.
    return record(snd_seq_drain_output(seq));
}

} // namespace MakeASound::ALSA
