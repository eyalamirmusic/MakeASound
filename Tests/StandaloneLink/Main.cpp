// An app over MakeASoundStandaloneCore that builds its plugin itself and defines
// no describeModule(): it links only while StandaloneHost's default constructor,
// the one member that names the module, stays out of the TU of the one it uses.
// Run, it builds the host with no settings file and checks it holds the plugin.

#include <MakeASound/Plugin/Standalone/StandaloneHost.h>

namespace
{
using namespace MakeASound;

struct SilentPlugin : Plugin
{
    std::string_view name() const override { return "StandaloneLink"; }

    BusLayout getBusLayout() const override { return BusLayout::stereoOut(); }

    void prepare(const ProcessSpec&) override {}

    void process(ProcessContext& context) noexcept override
    {
        for (auto channel: context.mainOutput())
            for (auto& sample: channel)
                sample = 0.f;
    }
};
} // namespace

int main()
{
    auto host = Standalone::StandaloneHost {EA::makeOwned<SilentPlugin>(), {}};
    return host.plugin().name() == "StandaloneLink" ? 0 : 1;
}
