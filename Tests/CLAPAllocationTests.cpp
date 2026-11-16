// The CLAP adapter's audio-thread paths under the allocation ban: a whole
// process() with values, notes, MIDI, SysEx and a transport, over varying block
// sizes, a reactivation and offline rendering; params.flush; the plugin's own
// moves; and the editor's gestures, held while processing and flushed while
// not. The drivers are CLAPAllocationDriver.h's, which a consumer's plugin runs
// through too; what is here is the fixtures and the checks their members allow.

#include "CLAPAllocationDriver.h"

#include <NanoTest/NanoTest.h>

namespace CLAPAllocationTests
{
using namespace nano;
using namespace TestPlugins;
using namespace CLAPHost;
using namespace CLAPAllocation;

namespace
{
auto tEffectProcess = test("Allocations/clapEffectProcessIsOffTheHeap") = []
{
    for (auto inPlace: {false, true})
    {
        auto host = PluginHost<GainPlugin> {};
        checkProcessIsOffTheHeap(host, inPlace);
        check(host.plugin().processed > 0);
    }
};

auto tInstrumentProcess = test("Allocations/clapInstrumentProcessIsOffTheHeap") = []
{
    auto host = PluginHost<SynthPlugin> {};
    checkProcessIsOffTheHeap(host, false);

    fillBusyBlock(host, maxBlock, false, 0);
    host.run();
    check(!host.out.ofType(CLAP_EVENT_MIDI).empty());
    check(!host.out.ofType(CLAP_EVENT_MIDI_SYSEX).empty());
};

auto tAddedBypass = test("Allocations/clapAddedBypassIsOffTheHeap") = []
{
    auto host = PluginHost<LatentEffect> {};
    checkProcessIsOffTheHeap(host, false);
    check(host.plugin().bypassed > 0);
};

auto tFlush = test("Allocations/clapFlushIsOffTheHeap") = []
{
    auto effect = PluginHost<GainPlugin> {};
    check(flushAllocations(effect) == 0);

    auto instrument = PluginHost<SynthPlugin> {};
    check(flushAllocations(instrument) == 0);
};

auto tPluginSideChanges = test("Allocations/clapPluginSideChangesAreOffTheHeap") = []
{
    auto host = PluginHost<GainPlugin> {};
    check(pluginSideChangesAllocations(host) == 0);

    auto moves = PluginHost<MoveReportPlugin> {};
    check(pluginSideChangesAllocations(moves) == 0);
};

auto tHeldPath = test("Allocations/clapHeldParameterPathIsOffTheHeap") = []
{
    auto host = PluginHost<GainPlugin> {};
    check(heldPathAllocations(host) == 0);
    check(!host.out.ofType(CLAP_EVENT_PARAM_GESTURE_END).empty());
};

auto tIdleEdits = test("Allocations/clapIdleEditorEditsAreOffTheHeap") = []
{
    auto host = PluginHost<GainPlugin> {};
    check(idleEditAllocations(host) == 0);
    check(host.host.flushRequests > 0);
    check(host.out.ofType(CLAP_EVENT_PARAM_GESTURE_BEGIN).size() == 5);
};
} // namespace
} // namespace CLAPAllocationTests
