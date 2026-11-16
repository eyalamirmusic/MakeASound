#pragma once

// The CLAP adapter's audio-thread paths under the allocation ban, over any
// plugin: what CLAPAllocationTests.cpp measures with its fixtures, and what
// makeasound_add_allocation_tests measures over every class of a consumer's
// describeModule() (PluginAllocations/CLAP.cpp). Each driver returns how many
// times the heap was reached for; the checks a fixture's members allow stay in
// the fixture suite.

#include "AllocationProbe.h"
#include "CLAPTestHost.h"

#include <NanoTest/NanoTest.h>

#include <algorithm>
#include <string_view>
#include <vector>

namespace CLAPAllocation
{
using namespace CLAPHost;
using Probe::allocationsIn;

// Every listed parameter's id and range, as a host reads them.
template <typename P>
std::vector<clap_param_info> listedParams(const PluginHost<P>& host)
{
    auto infos = std::vector<clap_param_info> {};

    for (auto i = 0u; i < host.params->count(host.clap); ++i)
        infos.push_back(host.info(static_cast<int>(i)));

    return infos;
}

inline double between(const clap_param_info& info, double amount)
{
    return info.min_value + (info.max_value - info.min_value) * amount;
}

// Everything a busy host block carries, filled outside the measurement: two
// values for every listed parameter, a note on and off, a CC and a SysEx as
// MIDI, and a playing transport.
template <typename P>
void fillBusyBlock(PluginHost<P>& host, int numSamples, bool inPlace, int block)
{
    static constexpr uint8_t sysEx[] = {0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7};

    host.clearEvents();

    auto last = static_cast<uint32_t>(std::max(0, numSamples - 1));
    auto amount = block % 2 == 0 ? 0.3 : 0.7;

    host.in.note(CLAP_EVENT_NOTE_ON, 0, 60);

    for (const auto& info: listedParams(host))
        host.in.param(info.id, 0, between(info, 1.0 - amount));

    host.in.midi(last / 2, 0xB0, 1, 64);
    host.in.sysEx(last / 2, sysEx, sizeof(sysEx));

    for (const auto& info: listedParams(host))
        host.in.param(info.id, last, between(info, amount));

    host.in.note(CLAP_EVENT_NOTE_OFF, last, 60);

    host.transport = {};
    host.transport.header =
        headerOf(sizeof(clap_event_transport), CLAP_EVENT_TRANSPORT, 0);
    host.transport.flags =
        CLAP_TRANSPORT_HAS_TEMPO | CLAP_TRANSPORT_HAS_BEATS_TIMELINE
        | CLAP_TRANSPORT_HAS_TIME_SIGNATURE | CLAP_TRANSPORT_IS_PLAYING;
    host.transport.tempo = 120.0;
    host.transport.song_pos_beats = block * CLAP_BEATTIME_FACTOR / 2;
    host.transport.tsig_num = 4;
    host.transport.tsig_denom = 4;
    host.useTransport = true;

    host.fillBlock(numSamples, inPlace);
}

// One unmeasured busy block, then five measured ones of varying sizes, an empty
// one among them.
template <typename P>
int allocationsOverBlocks(PluginHost<P>& host, bool inPlace, int maxSamples)
{
    auto count = 0;
    auto block = 0;

    fillBusyBlock(host, maxSamples, inPlace, block++);
    host.run();

    for (auto size: {64, 17, 1, 63, 0})
    {
        fillBusyBlock(host, std::min(size, maxSamples), inPlace, block++);
        count += allocationsIn([&] { host.run(); });
    }

    return count;
}

// Busy blocks at 48 kHz, then reactivated at 44.1 kHz in blocks of 32, then
// rendering offline.
template <typename P>
void checkProcessIsOffTheHeap(PluginHost<P>& host,
                              bool inPlace,
                              std::string_view what = {})
{
    nano::check(allocationsOverBlocks(host, inPlace, maxBlock) == 0, what);

    host.configure(44100.0, 32);
    nano::check(allocationsOverBlocks(host, inPlace, 32) == 0, what);

    host.render->set(host.clap, CLAP_RENDER_OFFLINE);
    nano::check(allocationsOverBlocks(host, inPlace, 32) == 0, what);
    host.render->set(host.clap, CLAP_RENDER_REALTIME);
}

// params.flush with a value for every listed parameter, active but not
// processing, as a host flushes on its audio thread between blocks.
template <typename P>
int flushAllocations(PluginHost<P>& host)
{
    host.clap->stop_processing(host.clap);
    auto count = 0;

    for (auto amount: {0.25, 0.75})
    {
        host.clearEvents();

        for (const auto& info: listedParams(host))
            host.in.param(info.id, 0, between(info, amount));

        count += allocationsIn([&] { host.flush(); });
    }

    host.clap->start_processing(host.clap);
    return count;
}

// The plugin moves every listed parameter itself between blocks (a preset of
// its own), and each block sends a value per parameter that reports plugin
// moves (ParameterList::Entry::reportsPluginMoves).
template <typename P>
int pluginSideChangesAllocations(PluginHost<P>& host, std::string_view what = {})
{
    const auto& list = host.plugin().parameters();
    auto& wrapper = host.adapter().wrapper();
    auto infos = listedParams(host);
    auto count = 0;
    auto block = 0;
    auto reported = static_cast<size_t>(
        std::count_if(infos.begin(),
                      infos.end(),
                      [&](const auto& info)
                      {
                          for (const auto& entry: list)
                              if (entry.clapId == info.id)
                                  return entry.reportsPluginMoves;

                          return false;
                      }));

    auto moveEveryParameter = [&]
    {
        host.clearEvents();
        host.useTransport = false;

        for (auto i = 0; i < list.size(); ++i)
            if (list.isListedInCLAP(i))
                wrapper.setNormalizedParameter(i, block % 2 == 0 ? 0.1f : 0.9f);
    };

    moveEveryParameter();
    host.fillBlock(maxBlock);
    host.run();

    for (auto size: {64, 17, 1, 0, 63})
    {
        ++block;
        moveEveryParameter();
        host.fillBlock(std::min(size, maxBlock));
        count += allocationsIn([&] { host.run(); });
        nano::check(host.out.ofType(CLAP_EVENT_PARAM_VALUE).size()
                        == std::min(reported, size_t {maxEvents}),
                    what);
    }

    return count;
}

// The held path an editor gesture takes while processing: every parameter
// begun and performed, a busy block while they are held, then every gesture
// ended and the block that carries the ends.
template <typename P>
int heldPathAllocations(PluginHost<P>& host)
{
    auto& listener = *host.plugin().hostEditListener();
    auto& params = host.plugin().parameters();
    auto numParams = params.size();

    fillBusyBlock(host, maxBlock, false, 0);
    host.run();

    auto count = allocationsIn(
        [&]
        {
            for (auto i = 0; i < numParams; ++i)
            {
                listener.beginParameterEdit(i);
                listener.performParameterEdit(i, 0.5f);
            }
        });

    fillBusyBlock(host, maxBlock, false, 1);
    count += allocationsIn([&] { host.run(); });

    count += allocationsIn(
        [&]
        {
            for (auto i = 0; i < numParams; ++i)
                listener.endParameterEdit(i);
        });

    host.clearEvents();
    host.fillBlock(maxBlock);
    count += allocationsIn([&] { host.run(); });
    return count;
}

// The same gestures while the host is not processing: each asks for a flush,
// and the flush sends them, the last thing in `host.out`. First gestures made
// while processing that no block carried: stop_processing (the audio thread)
// asks for a callback, which asks for the flush. Then a host list that takes
// one event per flush, which leaves the rest pending for the next.
template <typename P>
int idleEditAllocations(PluginHost<P>& host)
{
    auto& listener = *host.plugin().hostEditListener();
    auto numParams = host.plugin().parameters().size();

    auto gestures = [&](float value)
    {
        for (auto i = 0; i < numParams; ++i)
        {
            listener.beginParameterEdit(i);
            listener.performParameterEdit(i, value);
            listener.endParameterEdit(i);
        }
    };

    auto count = allocationsIn([&] { gestures(0.75f); });
    count += allocationsIn([&] { host.clap->stop_processing(host.clap); });
    host.clap->on_main_thread(host.clap);
    host.clearEvents();
    count += allocationsIn([&] { host.flush(); });

    gestures(0.5f);
    host.out.limit = 1;

    for (auto drain = 0; drain < 4; ++drain)
    {
        host.clearEvents();
        count += allocationsIn([&] { host.flush(); });
    }

    host.out.limit = maxEvents;
    host.clearEvents();
    host.flush();

    host.clearEvents();
    count += allocationsIn([&] { gestures(0.25f); });
    count += allocationsIn([&] { host.flush(); });

    host.clap->start_processing(host.clap);
    return count;
}

} // namespace CLAPAllocation
