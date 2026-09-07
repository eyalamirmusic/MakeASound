#pragma once

#include "AudioEngine.h"

#include <string>

namespace AudioProbe
{

enum class ProbeStatus
{
    // Nothing has happened yet that would answer the question.
    Pending,
    Pass,
    Gap,
    NotApplicable
};

std::string toString(ProbeStatus status);
std::string toString(MS::DeviceNotification notification);

struct Probe
{
    std::string id;
    std::string expected;
    std::string actual;
    ProbeStatus status = ProbeStatus::Pending;
};

// The checks themselves: what a caller should be able to expect of MakeASound,
// and what it actually does on the machine this is running on. A Gap is a
// finding, not a bug in the probe.
class ProbeSet
{
public:
    explicit ProbeSet(AudioEngine& engineToUse);

    // Everything answerable from the API's own state. Cheap enough to call once
    // a second, which is what keeps the readings live.
    void refresh();

    // Live checks, driven by what the device did on its own.
    void observe(const DeviceEvent& event);

    const MS::Vector<Probe>& all() const { return probes; }
    const MS::SessionState& getSession() const { return session; }
    const std::string& getLastNotification() const { return lastNotification; }

    int count(ProbeStatus status) const;
    void logGaps() const;

private:
    Probe& get(std::string_view id);

    void refreshSession();
    void refreshDevices();
    void refreshDefaultInput();
    void refreshStream();
    void refreshHardwareRate();
    void refreshCallbackStatus();
    void refreshMidi();

    AudioEngine& engine;
    MS::Vector<Probe> probes;
    MS::Vector<MS::DeviceInfo> baseline;
    MS::SessionState session;
    std::string lastNotification {"none yet"};
    bool midiProbeRun = false;

    // Which delivery paths have been seen, kept because each notification arrives
    // twice — once on the thread that raised it, once from the queue.
    int queuedDeliveries = 0;
    bool callbackLeftTheMainThread = false;
};

} // namespace AudioProbe
