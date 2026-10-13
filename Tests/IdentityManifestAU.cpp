// The AU half of the identity manifest: the component's codes, the global
// parameter list in GetParameterList's order, each listed parameter's info in
// the host's value space, and the AUv3 tree's address and identifier.

#include "IdentityManifest.h"

#include <MakeASound/Plugin/AU/ComponentType.h>
#include <MakeASound/Plugin/AU/HostParameters.h>

#include <algorithm>

namespace
{
using namespace IdentityManifest;
using MakeASound::AU::ParameterSpace;

void describeAU(const ModuleDescription& module,
                const PluginDescription& plugin,
                Lines& out)
{
    auto component = MakeASound::AU::componentInfoFor(module, plugin);
    out.push_back(print("au component %s %s %s",
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
        ids += print(" %u", static_cast<unsigned>(list.entry(index).hostId));

    out.push_back("au list" + ids);

    for (auto index = 0; index < list.size(); ++index)
    {
        const auto& entry = list.entry(index);

        if (!space.isListed(list, index))
        {
            out.push_back(print("au param entry %d unlisted", index));
            continue;
        }

        auto info = AudioUnitParameterInfo {};
        space.fillInfo(entry, info);

        if (info.cfNameString != nullptr)
            CFRelease(info.cfNameString);

        auto position = std::find(order.begin(), order.end(), index) - order.begin();
        const auto& param = *entry.param;

        out.push_back(print(
            "au param entry %d id %u position %d min %.9g max %.9g default %.9g "
            "unit %u flags 0x%08x mid %.9g strings %d",
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

        out.push_back(print(
            "auv3 param entry %d address %llu identifier \"%s\" "
            "strings %d",
            index,
            static_cast<unsigned long long>(entry.address),
            entry.identifier.c_str(),
            space.hasValueStrings(entry, MakeASound::AU::AUFlavour::V3) ? 1 : 0));
    }
}

auto added = addSection({{"au", "auv3"}, describeAU});
} // namespace
