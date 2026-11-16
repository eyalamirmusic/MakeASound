// HostFixturePair: one module holding two plugins, a stereo effect and a mono
// one with no editor at all, for the suites that check a module is scanned as
// every plugin it holds.
// Built as CLAP only, whose factory lists every class of describeModule().

#include <MakeASound/Plugin/MakeASoundPlugin.h>

#include <string_view>

namespace
{
using namespace MakeASound;

struct PairPlugin : Plugin
{
    PairPlugin(std::string_view nameToUse, int channelsToUse)
        : pluginName(nameToUse)
        , channels(channelsToUse)
    {
    }

    std::string_view name() const override { return pluginName; }

    BusLayout getBusLayout() const override
    {
        return BusLayout::stereoInOut(channels);
    }

    // The mono one has none at all, so no clap.gui; the stereo one shows
    // GenericEditor wherever the plugin UI is built.
    bool hasEditor() const override { return channels != 1; }

    void prepare(const ProcessSpec&) override {}

    void process(ProcessContext& context) noexcept override
    {
        const auto& input = context.mainInput();
        auto& output = context.mainOutput();

        if (!input.isEmpty()
            && input.getChannelPointers() != output.getChannelPointers())
            output.copyFrom(input);
    }

    std::string_view pluginName;
    int channels = 2;
};
} // namespace

namespace MakeASound
{

ModuleDescription describeModule()
{
    auto module = ModuleDescription {};
    module.vendor = "MakeASound Tests";
    module.manufacturerCode = "MkTs";
    module.plugins.add(
        {.name = "HostFixturePairStereo",
         .category = Category::Effect,
         .pluginCode = "HfPs",
         .create = []
         { return EA::makeOwned<PairPlugin>("HostFixturePairStereo", 2); }});
    module.plugins.add(
        {.name = "HostFixturePairMono",
         .category = Category::Effect,
         .pluginCode = "HfPm",
         .create = []
         { return EA::makeOwned<PairPlugin>("HostFixturePairMono", 1); }});
    return module;
}

} // namespace MakeASound
