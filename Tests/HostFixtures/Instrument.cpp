// HostFixtureInstrument: MIDI in and out, a stereo output and no audio input. It
// echoes every MIDI event it gets to its MIDI output and plays a constant level
// while any note is held, so a host sees both directions.

#include <MakeASound/Plugin/MakeASoundPlugin.h>

namespace
{
using namespace MakeASound;

struct InstrumentParams : ParameterGroup
{
    InstrumentParams() { add(level); }

    FloatParam level {"Level", 0.f, 1.f, 0.8f, {.versionHint = 1}};
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

    void prepare(const ProcessSpec&) override { held = 0; }

    void process(ProcessContext& context) noexcept override
    {
        for (const auto& event: context.mainMidiIn())
        {
            if (event.isNoteOn())
                ++held;
            else if (event.isNoteOff() && held > 0)
                --held;

            if (context.mainMidiOut().size() < context.mainMidiOut().capacity())
                context.mainMidiOut().add(event);
        }

        auto value = held > 0 ? params.level.get() : 0.f;

        for (auto channel: context.mainOutput())
            for (auto& sample: channel)
                sample = value;
    }

    int held = 0;
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
