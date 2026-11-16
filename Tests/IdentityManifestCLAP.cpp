// The CLAP half of the identity manifest: the plugin id under the module's
// bundle id, and what the adapter's params extension publishes, as a host reads
// it through count and get_info: each clap_id, its flags, range, default and
// module, and the ports a host routes by.

#include <MakeASound/Plugin/CLAP/Adapter.h>
#include <MakeASound/Plugin/CLAP/Factory.h>
#include <MakeASound/Plugin/Core/Identity.h>

#include "IdentityManifest.h"

#include <memory>

namespace IdentityManifestCLAP
{
namespace
{
using namespace IdentityManifest;

void describeCLAP(const ModuleDescription&,
                  const PluginDescription& plugin,
                  Lines& out)
{
    out.push_back(
        print("clap plugin \"{}\"",
              clapIdFor(MakeASound::CLAP::moduleBundleId(), plugin).c_str()));

    auto adapter = std::make_unique<MakeASound::CLAP::Adapter>(
        nullptr, nullptr, plugin.create(), parameterIdentityFor(plugin));
    const auto& list = adapter->plugin().parameters();

    for (auto position = 0; position < adapter->paramCount(); ++position)
    {
        auto info = clap_param_info {};
        adapter->paramInfo(position, info);

        auto entry = -1;

        for (auto i = 0; i < list.size(); ++i)
            if (list.isListedInCLAP(i) && list.entry(i).clapId == info.id)
                entry = i;

        out.push_back(print("clap param {} entry {} id {} flags 0x{:x} min {:.9g} "
                            "max {:.9g} default {:.9g} module \"{}\"",
                            position,
                            entry,
                            static_cast<unsigned>(info.id),
                            static_cast<unsigned>(info.flags),
                            info.min_value,
                            info.max_value,
                            info.default_value,
                            static_cast<const char*>(info.module)));
    }

    const auto& layout = adapter->wrapper().busLayout();
    out.push_back(print("clap ports audio {}/{} note {}/{}",
                        layout.inputs.size(),
                        layout.outputs.size(),
                        layout.midiInputs.size(),
                        layout.midiOutputs.size()));
}

auto added = addSection({4, {"clap"}, describeCLAP});
} // namespace
} // namespace IdentityManifestCLAP
