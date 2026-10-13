// HostFixtureEffect: a stereo effect with a mono sidechain input and a skewed
// parameter, passing its main input through.

#include <MakeASound/Plugin/MakeASoundPlugin.h>

namespace
{
using namespace MakeASound;

struct EffectParams : ParameterGroup
{
    EffectParams() { add(cutoff); }

    FloatParam cutoff {
        "Cutoff", FloatRange::withCentre(20.f, 20000.f, 1000.f), 1000.f};
};

struct EffectPlugin : StatePlugin<State<EffectParams>>
{
    std::string_view name() const override { return "HostFixtureEffect"; }

    BusLayout getBusLayout() const override
    {
        auto layout = BusLayout::stereoInOut();
        layout.inputs.add({.name = "Sidechain", .numChannels = 1, .isMain = false});
        return layout;
    }

    void prepare(const ProcessSpec&) override {}
    void process(ProcessContext&) noexcept override {}
};
} // namespace

namespace MakeASound
{

ModuleDescription describeModule()
{
    auto module = ModuleDescription {};
    module.vendor = "MakeASound Tests";
    module.manufacturerCode = "MkTs";
    module.plugins.add({.name = "HostFixtureEffect",
                        .category = Category::Effect,
                        .pluginCode = "HfEf",
                        .create = [] { return EA::makeOwned<EffectPlugin>(); }});
    return module;
}

} // namespace MakeASound
