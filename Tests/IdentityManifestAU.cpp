// The AU half of the identity manifest: the component's codes, the global
// parameter list in GetParameterList's order, each listed parameter's info in
// the host's value space, and the AUv3 tree's address and identifier.

#include "IdentityManifest.h"
#include "TestPlugins.h"

#include <MakeASound/Plugin/AU/ComponentType.h>
#include <MakeASound/Plugin/AU/HostParameters.h>

#include <NanoTest/NanoTest.h>

#include <algorithm>

namespace IdentityManifestAU
{
namespace
{
using namespace IdentityManifest;
using MakeASound::AU::ParameterSpace;

void describeAU(const ModuleDescription& module,
                const PluginDescription& plugin,
                Lines& out)
{
    auto component = MakeASound::AU::componentInfoFor(module, plugin);
    out.push_back(print("au component {} {} {}",
                        fourCC(component.type).c_str(),
                        fourCC(component.subtype).c_str(),
                        fourCC(component.manufacturer).c_str()));

    auto wrapper = PluginWrapper(
        plugin.create(), PluginFormat::AU, parameterIdentityFor(plugin));
    const auto& list = wrapper.plugin().parameters();
    auto space = ParameterSpace(plugin);

    auto order = space.listOrder(list);
    auto ids = std::string {};

    for (auto index: order)
        ids += print(" {}", static_cast<unsigned>(list.entry(index).hostId));

    out.push_back("au list" + ids);

    for (auto index = 0; index < list.size(); ++index)
    {
        const auto& entry = list.entry(index);

        if (!space.isListed(list, index))
        {
            out.push_back(print("au param entry {} unlisted", index));
            continue;
        }

        // As GetParameterInfo gives it, the clump included.
        auto info = AudioUnitParameterInfo {};
        space.fillInfo(entry, info);
        space.addClump(entry, info);

        if (info.cfNameString != nullptr)
            CFRelease(info.cfNameString);

        auto position = std::find(order.begin(), order.end(), index) - order.begin();
        const auto& param = *entry.param;

        out.push_back(print(
            "au param entry {} id {} position {} min {:.9g} max {:.9g} "
            "default {:.9g} unit {} flags 0x{:08x} mid {:.9g} strings {}",
            index,
            static_cast<unsigned>(entry.hostId),
            static_cast<int>(position),
            info.minValue,
            info.maxValue,
            info.defaultValue,
            static_cast<unsigned>(info.unit),
            static_cast<unsigned>(info.flags),
            space.toHost(entry, param.toPlain(0.5f)),
            space.hasValueStrings(entry, MakeASound::AU::AUFlavour::V2) ? 1 : 0));

        // Only where it has one, so an ungrouped plugin's manifest reads the same.
        if ((info.flags & kAudioUnitParameterFlag_HasClump) != 0)
        {
            auto* name = space.copyClumpName(list, info.clumpID);
            out.push_back(print("au clump entry {} id {} name \"{}\"",
                                index,
                                static_cast<unsigned>(info.clumpID),
                                MakeASound::AU::toUtf8(name).c_str()));

            if (name != nullptr)
                CFRelease(name);
        }

        out.push_back(print(
            "auv3 param entry {} address {} identifier \"{}\" "
            "strings {}",
            index,
            static_cast<unsigned long long>(entry.address),
            entry.identifier.c_str(),
            space.hasValueStrings(entry, MakeASound::AU::AUFlavour::V3) ? 1 : 0));
    }
}

auto added = addSection({2, {"au", "auv3"}, describeAU});

using namespace nano;

// A legacy port's post-port parameter lists after every indexed one, and the
// SDK's indexed list, ids 0..n-1, gives way to the adapter's own.
auto tPostPortListing = test("Identity/aPostPortParameterListsLastInAnAU") = []
{
    auto create = []
    { return OwningPointer<Plugin>(EA::makeOwned<TestPlugins::NewerGainPlugin>()); };
    auto description = PluginDescription {.name = "Newer",
                                          .pluginCode = "Nwr1",
                                          .create = create,
                                          .idScheme = IdScheme::JUCELegacyIndex};

    auto every = PluginWrapper(
        description.create(), PluginFormat::AU, parameterIdentityFor(description));
    auto everySpace = ParameterSpace(description);
    check(everySpace.usesSdkIndexedList(every.plugin().parameters()));

    description.overrides.indexedThroughVersion = 1;
    auto wrapper = PluginWrapper(
        description.create(), PluginFormat::AU, parameterIdentityFor(description));
    const auto& list = wrapper.plugin().parameters();
    auto space = ParameterSpace(description);
    auto order = space.listOrder(list);

    check(!space.usesSdkIndexedList(list));
    check(list.entry(order.back()).id == "Drive");

    for (auto slot = 0; slot + 1 < order.size(); ++slot)
        check(list.entry(order[slot]).hostId == static_cast<uint32_t>(slot));
};
} // namespace
} // namespace IdentityManifestAU
