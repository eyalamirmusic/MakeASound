#include "Probes.h"

#include <eacp/Core/Utils/Logging.h>

#include <cmath>

namespace AudioProbe
{
namespace
{
std::string milliseconds(double seconds)
{
    auto rounded = std::round(seconds * 10000.0) / 10.0;
    auto text = std::to_string(rounded);
    return text.substr(0, text.find('.') + 2) + " ms";
}

Probe makeProbe(std::string id, std::string expected)
{
    auto probe = Probe {};
    probe.id = std::move(id);
    probe.expected = std::move(expected);

    return probe;
}

std::string rateList(const MS::Vector<int>& rates)
{
    auto text = std::string {};

    for (auto rate: rates)
    {
        if (!text.empty())
            text += ", ";

        text += std::to_string(rate);
    }

    return text;
}
} // namespace

std::string toString(ProbeStatus status)
{
    switch (status)
    {
        case ProbeStatus::Pass:
            return "pass";
        case ProbeStatus::Gap:
            return "GAP";
        case ProbeStatus::NotApplicable:
            return "n/a";
        case ProbeStatus::Pending:
        default:
            return "waiting";
    }
}

std::string toString(MS::DeviceNotification notification)
{
    switch (notification)
    {
        case MS::DeviceNotification::Started:
            return "Started";
        case MS::DeviceNotification::Stopped:
            return "Stopped";
        case MS::DeviceNotification::Rerouted:
            return "Rerouted";
        case MS::DeviceNotification::InterruptionBegan:
            return "InterruptionBegan";
        case MS::DeviceNotification::InterruptionEnded:
            return "InterruptionEnded";
        case MS::DeviceNotification::Unlocked:
            return "Unlocked";
        default:
            return "Unknown";
    }
}

ProbeSet::ProbeSet(AudioEngine& engineToUse)
    : engine(engineToUse)
{
    probes.add(
        makeProbe("session/owned-by-app",
                  "the app chooses the session category and when it is activated"));

    probes.add(makeProbe("session/microphone-cost",
                         "a playback-only app needs no microphone permission"));

    probes.add(
        makeProbe("config/playback-only",
                  "getDefaultConfig() can be asked for a playback-only stream"));

    probes.add(
        makeProbe("devices/default-flag",
                  "getDefaultOutputDevice() comes back flagged isDefaultOutput"));

    probes.add(makeProbe("devices/rate-choices",
                         "the default output offers the rates a picker could show"));

    probes.add(
        makeProbe("devices/default-input-flag",
                  "getDefaultInputDevice() names the device the platform does"));

    probes.add(
        makeProbe("devices/route-stability",
                  "a device id keeps naming the same device across a route change"));

    probes.add(
        makeProbe("stream/negotiated-block-size",
                  "the block size the device runs is readable from the manager"));

    probes.add(
        makeProbe("stream/running-after-os-stop",
                  "isRunning() says false once the OS has stopped the device"));

    probes.add(makeProbe("notify/main-thread-delivery",
                         "device notifications arrive somewhere a UI can use them"));

    probes.add(makeProbe("latency/includes-route",
                         "getStreamLatency() counts the route's own latency"));

    probes.add(
        makeProbe("midi/virtual-port-errors",
                  "the MIDI facade reports failures the way the audio one does"));

    probes.add(makeProbe("callback/dirty-on-shape-change",
                         "the first block of a new stream shape reports dirty"));

    probes.add(makeProbe("stream/hardware-rate",
                         "the rate the stream reports is the rate the hardware runs"));

    probes.add(makeProbe("callback/status-reported",
                         "a block that missed its deadline says so in its status"));
}

Probe& ProbeSet::get(std::string_view id)
{
    for (auto& probe: probes)
        if (probe.id == id)
            return probe;

    return probes[0];
}

int ProbeSet::count(ProbeStatus status) const
{
    auto total = 0;

    for (const auto& probe: probes)
        if (probe.status == status)
            ++total;

    return total;
}

void ProbeSet::refresh()
{
    session = MS::getSessionState();

    refreshSession();
    refreshDevices();
    refreshDefaultInput();
    refreshStream();
    refreshCallbackStatus();
    refreshMidi();
}

void ProbeSet::refreshSession()
{
    const auto& before = engine.getSessionBeforeConstruction();
    const auto& after = engine.getSessionAfterConstruction();
    const auto& running = engine.getSessionAfterStart();

    auto& owned = get("session/owned-by-app");
    auto& microphone = get("session/microphone-cost");

    if (!after.available)
    {
        owned.status = ProbeStatus::NotApplicable;
        owned.actual = "no audio session on this platform";

        microphone.status = ProbeStatus::NotApplicable;
        microphone.actual = "microphone access is not gated by a session here";
        return;
    }

    auto name = [](const MS::SessionState& state)
    { return MS::getSessionCategoryName(state.category); };

    auto touched = before.category != after.category;

    if (touched)
    {
        owned.status = ProbeStatus::Gap;
        owned.actual = "DeviceManager's constructor moved the session from "
                       + name(before) + " to " + name(after)
                       + " before the app had said anything";
    }
    else
    {
        owned.status = ProbeStatus::Pass;
        owned.actual = "the constructor left the session on " + name(after)
                       + "; opening a playback-only stream set it to " + name(running)
                       + ", and setSessionConfig() overrides that";
    }

    auto claimsCapture = running.category == MS::SessionCategory::Record
                         || running.category == MS::SessionCategory::PlayAndRecord;

    microphone.status = claimsCapture ? ProbeStatus::Gap : ProbeStatus::Pass;
    microphone.actual =
        "a playback-only stream runs on " + name(running) + " and this bundle "
        + (hasMicUsageDescription()
               ? "carries NSMicrophoneUsageDescription, which it no longer needs"
               : "has no NSMicrophoneUsageDescription, and does not need one");
}

void ProbeSet::refreshDevices()
{
    auto& manager = engine.getManager();

    if (baseline.empty())
        baseline = manager.getDevices();

    auto fresh = manager.getDefaultOutputConfig();
    auto& playbackOnly = get("config/playback-only");

    if (!fresh.input.has_value())
    {
        playbackOnly.status = ProbeStatus::Pass;
        playbackOnly.actual =
            "getDefaultOutputConfig() claims no capture side; "
            "getDefaultDuplexConfig() is there for an app that wants one";
    }
    else
    {
        playbackOnly.status = ProbeStatus::Gap;
        playbackOnly.actual = "getDefaultOutputConfig() still claims '"
                              + fresh.input->device.name + "'";
    }

    auto output = manager.getDefaultOutputDevice();
    auto& flag = get("devices/default-flag");
    auto& rates = get("devices/rate-choices");

    if (!output.isValid())
    {
        flag.status = ProbeStatus::NotApplicable;
        flag.actual = "no output device on this machine";

        rates.status = ProbeStatus::NotApplicable;
        rates.actual = "no output device on this machine";
        return;
    }

    if (output.isDefaultOutput)
    {
        flag.status = ProbeStatus::Pass;
        flag.actual = "'" + output.name + "' is flagged as the default output";
    }
    else
    {
        flag.status = ProbeStatus::Gap;
        flag.actual = "'" + output.name
                      + "' came back with isDefaultOutput = false; the manager fell "
                        "back to the first device that had outputs";
    }

    if (output.sampleRates.size() > 1)
    {
        rates.status = ProbeStatus::Pass;
        rates.actual = "'" + output.name + "' offers "
                       + std::to_string(output.sampleRates.size())
                       + " rates: " + rateList(output.sampleRates);
    }
    else
    {
        rates.status = ProbeStatus::Gap;
        rates.actual = "'" + output.name + "' offers one rate ("
                       + rateList(output.sampleRates)
                       + "): the session's current rate is all the backend reports";
    }
}

// miniaudio marks every capture device of a duplex unit default on Core Audio, and
// playback is enumerated first, so a merged duplex entry used to outrank the device
// the user actually picked in System Settings.
void ProbeSet::refreshDefaultInput()
{
    auto& probe = get("devices/default-input-flag");
    auto platform = MS::getDefaultDeviceName(true);

    if (platform.empty())
    {
        probe.status = ProbeStatus::NotApplicable;
        probe.actual = "this platform will not name its own default input";
        return;
    }

    auto device = engine.getManager().getDefaultInputDevice();

    probe.status = device.name == platform ? ProbeStatus::Pass : ProbeStatus::Gap;
    probe.actual = "the platform calls '" + platform
                   + "' its default input and getDefaultInputDevice() says '"
                   + device.name + "'";
}

void ProbeSet::refreshStream()
{
    auto& manager = engine.getManager();
    auto stats = engine.getStats();

    auto& block = get("stream/negotiated-block-size");
    auto& dirty = get("callback/dirty-on-shape-change");
    auto& latency = get("latency/includes-route");

    if (stats.callbacks == 0)
    {
        block.status = ProbeStatus::Pending;
        block.actual = "no callback has run yet";

        dirty.status = ProbeStatus::Pending;
        dirty.actual = "no callback has run yet";
    }
    else
    {
        auto reported = manager.getStreamBlockSize();

        block.status =
            reported == stats.blockSize ? ProbeStatus::Pass : ProbeStatus::Gap;
        block.actual = "asked for " + std::to_string(engine.getRequestedBlockSize())
                       + ", getStreamBlockSize() says " + std::to_string(reported)
                       + " and the callback says " + std::to_string(stats.blockSize)
                       + " (last block " + std::to_string(stats.lastNumSamples) + ")";

        if (stats.firstBlockDirty)
        {
            dirty.status = ProbeStatus::Pass;
            dirty.actual = "the first block reported dirty; "
                           + std::to_string(stats.dirtyBlocks)
                           + " dirty blocks so far";
        }
        else
        {
            dirty.status = ProbeStatus::Gap;
            dirty.actual = "the first block of the stream did not report dirty";
        }
    }

    refreshHardwareRate();

    if (!session.available)
    {
        latency.status = ProbeStatus::NotApplicable;
        latency.actual = "no session latency to compare against on this platform";
        return;
    }

    if (stats.sampleRate <= 0)
    {
        latency.status = ProbeStatus::Pending;
        latency.actual = "no stream running";
        return;
    }

    auto reported = static_cast<double>(manager.getStreamLatency());
    auto reportedSeconds = reported / stats.sampleRate;
    auto ioBufferSeconds = session.sampleRate > 0
                               ? static_cast<double>(session.blockSize)
                                     / static_cast<double>(session.sampleRate)
                               : 0.0;
    auto routeSeconds = session.outputLatencySeconds + ioBufferSeconds;

    latency.status = reportedSeconds + 0.0005 < routeSeconds ? ProbeStatus::Gap
                                                             : ProbeStatus::Pass;

    latency.actual =
        "getStreamLatency() = " + std::to_string(manager.getStreamLatency())
        + " frames (" + milliseconds(reportedSeconds) + "); the route itself adds "
        + milliseconds(session.outputLatencySeconds) + " output latency + "
        + milliseconds(ioBufferSeconds) + " IO buffer";
}

// Asking for a rate the device is not on leaves the backend resampling, which costs
// latency and CPU and which nothing in the API would otherwise reveal.
void ProbeSet::refreshHardwareRate()
{
    auto& probe = get("stream/hardware-rate");
    auto reported = engine.getManager().getStreamSampleRate();

    if (reported <= 0 || !engine.getConfig().output.has_value())
    {
        probe.status = ProbeStatus::Pending;
        probe.actual = "no stream running";
        return;
    }

    auto hardware = MS::getCurrentSampleRate(engine.getConfig().output->device);

    if (hardware <= 0)
    {
        probe.status = ProbeStatus::NotApplicable;
        probe.actual = "this platform will not say what the device is clocked at";
        return;
    }

    probe.status = hardware == reported ? ProbeStatus::Pass : ProbeStatus::Gap;
    probe.actual = "getStreamSampleRate() says " + std::to_string(reported)
                   + " and the device is clocked at " + std::to_string(hardware)
                   + (hardware == reported ? "" : "; the backend is resampling");
}

// Nothing in an ordinary run misses a deadline, so the engine holds one block past
// its own to find out whether a dropout is visible at all.
void ProbeSet::refreshCallbackStatus()
{
    auto& probe = get("callback/status-reported");
    auto stats = engine.getStats();

    if (!stats.stallDone)
    {
        probe.status = ProbeStatus::Pending;
        probe.actual = "waiting for the deliberate stall";
        return;
    }

    auto reported = stats.underflows + stats.overflows;

    probe.status = reported > 0 ? ProbeStatus::Pass : ProbeStatus::Gap;
    probe.actual =
        "a callback was held three block durations past its deadline and "
        + (reported > 0
               ? std::to_string(reported) + " block(s) came back non-OK ("
                     + std::to_string(stats.underflows) + " underflow, "
                     + std::to_string(stats.overflows) + " overflow)"
               : std::string {"every block since still reported OK"});
}

void ProbeSet::refreshMidi()
{
    if (midiProbeRun)
        return;

    midiProbeRun = true;
    auto& probe = get("midi/virtual-port-errors");

    // No try/catch: a facade that still threw would take the app down here, which is
    // the point of the probe.
    auto midi = MS::MidiManager {};
    auto error = midi.openVirtualOutput("MakeASound Probe");
    midi.closeOutput();

    probe.status = ProbeStatus::Pass;

    if (error == MS::Error::NoError)
        probe.actual = "a virtual output opened and closed, returning NoError";
    else
        probe.actual = "openVirtualOutput returned " + MS::getErrorMessage(error)
                       + " instead of throwing, and the manager stayed usable";
}

void ProbeSet::observe(const DeviceEvent& event)
{
    lastNotification = toString(event.type)
                       + (event.viaQueue ? " (drained)"
                          : event.onMainThread ? " (callback, main thread)"
                                               : " (callback, audio thread)");

    if (event.viaQueue && event.onMainThread)
        ++queuedDeliveries;

    if (!event.viaQueue && !event.onMainThread)
        callbackLeftTheMainThread = true;

    auto& delivery = get("notify/main-thread-delivery");

    if (queuedDeliveries > 0)
    {
        delivery.status = ProbeStatus::Pass;
        delivery.actual =
            std::to_string(queuedDeliveries)
            + " notification(s) came back from drainNotifications() on the thread "
              "that asked for them"
            + (callbackLeftTheMainThread
                   ? ", after the realtime callback had delivered on an OS audio "
                     "thread"
                   : "");
    }

    // Each notification arrives twice, once down each path: the type-specific
    // checks below run on the queued copy so they answer once.
    if (!event.viaQueue)
        return;

    if (event.type == MS::DeviceNotification::Stopped)
    {
        auto& running = get("stream/running-after-os-stop");

        if (engine.isAutoRecovering())
        {
            running.status = ProbeStatus::Pending;
            running.actual = "the OS stopped the device and auto-recover took it "
                             "back; turn auto-recover off to see what the API "
                             "reports on its own";
        }
        else
        {
            auto isRunning = engine.getManager().isRunning();

            running.status = isRunning ? ProbeStatus::Gap : ProbeStatus::Pass;
            running.actual =
                std::string {"the OS stopped the device and isRunning() "
                             "says "}
                + (isRunning ? "true" : "false") + " with auto-recover off";
        }
    }

    if (event.type != MS::DeviceNotification::Rerouted)
        return;

    auto& stability = get("devices/route-stability");
    auto now = engine.getManager().getDevices();
    auto moved = std::string {};

    for (const auto& was: baseline)
    {
        for (const auto& is: now)
        {
            if (is.id != was.id || is.name == was.name)
                continue;

            if (!moved.empty())
                moved += ", ";

            moved += "id " + std::to_string(was.id) + " was '" + was.name
                     + "' and is now '" + is.name + "'";
        }
    }

    if (moved.empty())
    {
        stability.status = ProbeStatus::Pass;
        stability.actual =
            "after the reroute every id still names the device it did";
    }
    else
    {
        stability.status = ProbeStatus::Gap;
        stability.actual =
            moved + "; a cached DeviceInfo::id now opens something else";
    }

    baseline = now;
}

void ProbeSet::logGaps() const
{
    for (const auto& probe: probes)
    {
        if (probe.status != ProbeStatus::Gap)
            continue;

        eacp::LOG("GAP ",
                  probe.id,
                  "\n  expected: ",
                  probe.expected,
                  "\n  actual:   ",
                  probe.actual);
    }

    eacp::LOG("probes: ",
              count(ProbeStatus::Gap),
              " gaps, ",
              count(ProbeStatus::Pass),
              " pass, ",
              count(ProbeStatus::Pending),
              " waiting, ",
              count(ProbeStatus::NotApplicable),
              " n/a");
}

} // namespace AudioProbe
