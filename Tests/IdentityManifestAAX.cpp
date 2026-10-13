// The AAX half of the identity manifest: the effect id, each accepted stem
// pair's native plug-in id, and the parameter id each registered entry takes.

#include <MakeASound/Plugin/AAX/Describe.h>
#include <MakeASound/Plugin/AAX/Parameters.h>

#include "IdentityManifest.h"

namespace
{
using namespace IdentityManifest;

void describeAAX(const ModuleDescription&,
                 const PluginDescription& plugin,
                 Lines& out)
{
    namespace AAX = MakeASound::AAX;

    out.push_back(print("aax effect \"%s\"",
                        aaxEffectIdFor(AAX::moduleBundleId(), plugin).c_str()));

    auto wrapper = PluginWrapper(
        plugin.create(), PluginFormat::AAX, parameterIdentityFor(plugin));
    const auto& instance = wrapper.plugin();
    auto pairs = AAX::stemPairs();
    auto natives = std::string {};

    for (auto index = 0; index < static_cast<int>(pairs.size()); ++index)
        if (auto layout = AAX::layoutFor(wrapper.busLayout(), pairs[index]))
            if (instance.acceptsLayout(*layout))
                natives +=
                    print(" %d:0x%08x", index, AAX::nativePluginId(plugin, index));

    out.push_back("aax native" + natives);

    const auto& list = instance.parameters();

    for (auto index = 0; index < list.size(); ++index)
        if (list.isRegistered(index))
            out.push_back(print(
                "aax param entry %d id \"%s\" automatable %d",
                index,
                std::string(AAX::registeredId(list.entry(index).aaxId)).c_str(),
                list.entry(index).automatable ? 1 : 0));
}

auto added = addSection({{"aax"}, describeAAX});
} // namespace
