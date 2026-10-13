// A session the AUv3 saved (iPad Logic) opened by the v2 component on a Mac: the
// AUv3's fullState dictionary handed to the v2 unit as ClassInfo, both units
// registered in process under the same subtype and manufacturer.

#include "AUTestHost.h"
#include "AUv3TestHost.h"

#include <NanoTest/NanoTest.h>

#include <eacp/Core/Threads/EventLoop.h>

#include <cmath>

using namespace nano;
using namespace TestPlugins;

namespace
{
bool near(double a, double b, double tolerance = 1e-4)
{
    return std::abs(a - b) < tolerance;
}

template <typename Predicate>
bool pumpUntil(Predicate ready)
{
    return eacp::Threads::runEventLoopUntil(ready, eacp::Time::MS {5000});
}

OSStatus restoreInto(AUHost::UnitHost<GainPlugin>& host,
                     NSDictionary<NSString*, id>* state)
{
    auto info = (__bridge CFPropertyListRef) state;
    return AudioUnitSetProperty(host.unit,
                                kAudioUnitProperty_ClassInfo,
                                kAudioUnitScope_Global,
                                0,
                                &info,
                                sizeof(info));
}

auto tAUv3ToV2 = test("AUv3/aSessionTheAUv3SavedLoadsInTheV2Component") = []
{
    // The v2 factory first: once registerSubclass has claimed a description,
    // AudioComponentRegister of the same one hands back a component that
    // AudioComponentInstanceNew cannot open in process. A full run registers the
    // v2 suite's components long before; a run of this case alone does not.
    AUHost::registerTestComponents();

    auto source = AUv3Host::UnitHost<GainPlugin>("Gain");
    auto& saved = source.plugin().params;
    saved.gain.setValue(-11.f);
    saved.mode.setValue(1.f);
    saved.width.setValue(0.8f);
    source.plugin().state.preset = "From iPad";
    source.plugin().state.markChanged();
    pumpUntil([&] { return source.plugin().isStateSnapshotCurrent(); });

    for (auto* state: {source.unit.fullStateForDocument, source.unit.fullState})
    {
        auto target = AUHost::UnitHost<GainPlugin>("Gain");
        check(restoreInto(target, state) == noErr);

        const auto& params = target.plugin().params;
        check(near(params.gain.getValue(), -11.f));
        check(params.mode.getIndex() == 1);
        check(near(target.hostValue(target.hostIdOf(params.gain)), -11.f));
        check(pumpUntil([&] { return target.plugin().state.preset == "From iPad"; }));
    }

    // The document form carries the session-only parameter as well.
    auto target = AUHost::UnitHost<GainPlugin>("Gain");
    check(restoreInto(target, source.unit.fullStateForDocument) == noErr);
    check(near(target.plugin().params.width.getValue(), 0.8f));
};
} // namespace
