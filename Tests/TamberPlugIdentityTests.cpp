// IdScheme::TamberPlug: a port of a Tamber Plug plugin keeps the ids Plug's
// wrappers gave it (Plug's Lib/TamberPlug/VST3/Adapter.cpp registerParameters
// and AU/Adapter.cpp GetParameterList: the flat leaf index, hidden leaves
// counted and never registered), and a later release inserts parameters
// without moving them. The format suites check what each adapter publishes.

#include "TamberPlugFixtures.h"

#include <MakeASound/Plugin/MakeASoundPlugin.h>

#include <NanoTest/NanoTest.h>

#include <cstdint>
#include <optional>
#include <string_view>

using namespace nano;
using namespace MakeASound;

namespace
{
template <typename P>
PluginDescription plugPort(std::optional<int> indexedThrough = {})
{
    return {.name = "Plug Port",
            .pluginCode = "Plgp",
            .create = [] { return OwningPointer<Plugin>(EA::makeOwned<P>()); },
            .idScheme = IdScheme::TamberPlug,
            .overrides = {.indexedThroughVersion = indexedThrough}};
}

OwningPointer<PluginWrapper> wrap(const PluginDescription& description)
{
    return EA::makeOwned<PluginWrapper>(description.create(),
                                        PluginFormat::Unknown,
                                        parameterIdentityFor(description));
}

uint32_t idOf(const ParameterList& list, std::string_view id)
{
    return list.entry(list.indexOf(id)).hostId;
}

auto tIndexIds = test("TamberPlug/idsAreTheLeafIndexAsPlugGaveThem") = []
{
    auto wrapper = wrap(plugPort<TamberPlugFixtures::SVFPort>());
    const auto& list = wrapper->plugin().parameters();

    // HP, BP, LP, each Enable, Cutoff, Resonance: Plug's AU and VST3 ids 0..8.
    for (auto index = 0; index < TamberPlugFixtures::svfParameterCount; ++index)
    {
        check(list.entry(index).hostId == static_cast<uint32_t>(index));
        check(list.isRegistered(index));
    }

    check(idOf(list, "BP/Cutoff") == 4u);
    check(list.identityProblems().empty());
};

auto tSparse = test("TamberPlug/aHiddenLeafKeepsTheIdsSparse") = []
{
    auto wrapper = wrap(plugPort<TamberPlugFixtures::SparsePort>());
    const auto& list = wrapper->plugin().parameters();

    check(idOf(list, "Level") == 0u);
    check(idOf(list, "Internal") == 1u);
    check(idOf(list, "Tone") == 2u);

    // Counted, never registered or listed.
    check(!list.isRegistered(list.indexOf("Internal")));
    check(list.isRegistered(list.indexOf("Tone")));
};

auto tAddedBypass = test("TamberPlug/theAddedBypassKeepsItsOwnId") = []
{
    auto wrapper = wrap(plugPort<TamberPlugFixtures::SVFPort>());
    const auto& list = wrapper->plugin().parameters();
    const auto& bypass = list.entry(list.bypassIndex());

    // Plug added none: MakeASound's, out of the way of every index, and left to
    // the AU bypass property.
    check(bypass.addedBypass);
    check(bypass.hostId == 0x54D1526Fu);
    check(!bypass.listedInAU);
    check(list.isRegistered(list.bypassIndex()));
};

auto tOtherIds = test("TamberPlug/auv3AndAAXIdsAreNative") = []
{
    auto wrapper = wrap(plugPort<TamberPlugFixtures::SVFPort>());
    const auto& entry = wrapper->plugin().parameters().entry(4);

    check(entry.address == 4u);
    check(entry.identifier == "BP/Cutoff");
    check(entry.aaxId == "00000004");
    check(entry.groups[0].identifier == "BP");

    auto description = plugPort<TamberPlugFixtures::SVFPort>();
    auto policy = identityPolicyFor(description);
    check(policy.vst3Class == Vst3ClassRule::Native);
    check(policy.aaxPlugin == AAXPluginRule::Native);
};

auto tInserted = test("TamberPlug/aPostPortParameterMidTreeMovesNoOldId") = []
{
    auto shipped = wrap(plugPort<TamberPlugFixtures::SVFPort>());
    auto next = wrap(plugPort<TamberPlugFixtures::NewerSVFPort>(1));

    const auto& before = shipped->plugin().parameters();
    const auto& after = next->plugin().parameters();

    for (const auto& old: before)
    {
        const auto& now = after.entry(after.indexOf(std::string_view {old.id}));
        check(now.hostId == old.hostId);
        check(now.address == old.address);
        check(now.aaxId == old.aaxId);
    }

    const auto& drive = after.entry(after.indexOf("BP/Drive"));
    check(drive.postPort);
    check(drive.hostId == hostIdFor("BP/Drive"));
    check(after.identityProblems().empty());

    // Without the threshold every entry is indexed, and the insert shifts
    // the rest, as it did in Plug.
    auto unmarked = wrap(plugPort<TamberPlugFixtures::NewerSVFPort>());
    const auto& shifted = unmarked->plugin().parameters();
    check(idOf(shifted, "BP/Drive") == 4u);
    check(idOf(shifted, "BP/Cutoff") == 5u);
};

} // namespace
