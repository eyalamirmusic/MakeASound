// HostFixtureInstrument: MIDI in and out, a stereo output and no audio input.

#include <MakeASound/Plugin/MakeASoundPlugin.h>

namespace
{
using namespace MakeASound;

struct InstrumentParams : ParameterGroup
{
    InstrumentParams() { add(level); }

    FloatParam level {"Level", 0.f, 1.f, 0.8f};
};

struct InstrumentPlugin : StatePlugin<State<InstrumentParams>>
{
    std::string_view name() const override { return "HostFixtureInstrument"; }

    BusLayout getBusLayout() const override
    {
        auto layout = BusLayout::instrument();
        layout.midiOutputs.add({"MIDI Out"});
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
    module.plugins.add({.name = "HostFixtureInstrument",
                        .category = Category::Instrument,
                        .pluginCode = "HfIn",
                        .create = [] { return EA::makeOwned<InstrumentPlugin>(); }});
    return module;
}

} // namespace MakeASound
