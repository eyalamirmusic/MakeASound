#include "Probes.h"

#include <eacp/Core/Utils/Logging.h>

#include <cmath>
#include <exception>

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

std::string shortCategory(const std::string& category)
{
    constexpr auto prefix = std::string_view {"AVAudioSessionCategory"};

    if (category.rfind(prefix, 0) == 0)
        return category.substr(prefix.size());

    return category;
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
    session = snapshotSession();

    refreshSession();
    refreshDevices();
    refreshStream();
    refreshMidi();
}

void ProbeSet::refreshSession()
{
    const auto& before = engine.getSessionBeforeConstruction();
    const auto& after = engine.getSessionAfterConstruction();

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

    auto touched = before.category != after.category
                   || before.outputChannels != after.outputChannels;

    owned.status = touched ? ProbeStatus::Gap : ProbeStatus::Pass;
    owned.actual = "DeviceManager's constructor moved the session from "
                   + shortCategory(before.category) + " to "
                   + shortCategory(after.category) + " and its output channels from "
                   + std::to_string(before.outputChannels) + " to "
                   + std::to_string(after.outputChannels)
                   + "; MakeASound exposes no way to choose either";

    auto claimsCapture = after.category.find("PlayAndRecord") != std::string::npos
                         || after.category.find("Record") != std::string::npos;

    microphone.status = claimsCapture ? ProbeStatus::Gap : ProbeStatus::Pass;
    microphone.actual =
        "the session is " + shortCategory(after.category) + " and this bundle "
        + (after.micUsageDescription
               ? "carries NSMicrophoneUsageDescription to survive it"
               : "has no NSMicrophoneUsageDescription, which iOS terminates for");
}

void ProbeSet::refreshDevices()
{
    auto& manager = engine.getManager();

    if (baseline.empty())
        baseline = manager.getDevices();

    auto fresh = manager.getDefaultConfig();
    auto& playbackOnly = get("config/playback-only");

    if (!fresh.input.has_value())
    {
        playbackOnly.status = ProbeStatus::Pass;
        playbackOnly.actual = "this machine has no capture side to claim";
    }
    else
    {
        playbackOnly.status = ProbeStatus::Gap;
        playbackOnly.actual =
            "the default config claims '" + fresh.input->device.name + "' ("
            + std::to_string(fresh.input->nChannels)
            + " ch); a playback-only app has to know to call config.input.reset()";
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
        block.status = ProbeStatus::Gap;
        block.actual = "asked for " + std::to_string(engine.getRequestedBlockSize())
                       + ", the device runs " + std::to_string(stats.blockSize)
                       + " (last block " + std::to_string(stats.lastNumSamples)
                       + "); only AudioCallbackInfo says so, there is no "
                         "getStreamBlockSize() beside getStreamSampleRate()";

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
    auto routeSeconds = session.outputLatencySeconds + session.ioBufferSeconds;

    latency.status = reportedSeconds + 0.0005 < routeSeconds ? ProbeStatus::Gap
                                                             : ProbeStatus::Pass;

    latency.actual =
        "getStreamLatency() = " + std::to_string(manager.getStreamLatency())
        + " frames (" + milliseconds(reportedSeconds) + "); the route itself adds "
        + milliseconds(session.outputLatencySeconds) + " output latency + "
        + milliseconds(session.ioBufferSeconds) + " IO buffer";
}

void ProbeSet::refreshMidi()
{
    if (midiProbeRun)
        return;

    midiProbeRun = true;
    auto& probe = get("midi/virtual-port-errors");

    try
    {
        auto midi = MS::MidiManager {};
        midi.openVirtualOutput("MakeASound Probe");
        midi.closeOutput();

        probe.status = ProbeStatus::Pass;
        probe.actual = "a virtual output opened and closed without throwing";
    }
    catch (const std::exception& error)
    {
        probe.status = ProbeStatus::Gap;
        probe.actual = std::string {"openVirtualOutput threw: "} + error.what()
                       + "; the audio facade returns an Error for the same kind of "
                         "failure";
    }
    catch (...)
    {
        probe.status = ProbeStatus::Gap;
        probe.actual = "openVirtualOutput threw something that is not a "
                       "std::exception";
    }
}

void ProbeSet::observe(const DeviceEvent& event)
{
    lastNotification =
        toString(event.type)
        + (event.fromMainThread ? " (main thread)" : " (audio thread)");

    auto& delivery = get("notify/main-thread-delivery");

    // A Started that follows our own start() call is on the main thread because
    // we were: only the ones the device raises by itself answer the question.
    if (!event.fromMainThread)
    {
        delivery.status = ProbeStatus::Gap;
        delivery.actual = toString(event.type)
                          + " arrived on an OS audio thread, from which no "
                            "DeviceManager method may be called; every UI has to "
                            "write the same marshalling";
    }
    else if (delivery.status != ProbeStatus::Gap)
    {
        delivery.status = ProbeStatus::Pending;
        delivery.actual = toString(event.type)
                          + " arrived on the main thread, but it followed our own "
                            "start(); waiting for one the device raises by itself";
    }

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
