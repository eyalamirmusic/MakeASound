// HostFixtureEffect: a stereo effect with a mono sidechain that starts off, for
// the hosting suites to load. It passes its main input through and has
//
//   Cutoff         a skewed range with a unit, for text through the controller
//   Latency        its own latency in samples, so a host sees it change
//   Alt Names      renames Cutoff to Frequency when a state is loaded with it on
//   Peak           written by the plugin: the main input's peak
//   Sidechain      written by the plugin: the sidechain's peak
//   Seen Tempo     written by the plugin: the playhead's tempo
//   Seen Playing   written by the plugin: whether the playhead is playing
//   Rate Fraction  written by the plugin: the fractional part of the rate it was
//                  prepared at, so a host sees whether its rate arrived unrounded

#include <MakeASound/Plugin/MakeASoundPlugin.h>

#include <algorithm>
#include <cmath>

namespace
{
using namespace MakeASound;

struct RenamableParam : FloatParam
{
    using FloatParam::FloatParam;
    using FloatParam::setDisplayInfo;
};

struct EffectParams : ParameterGroup
{
    EffectParams()
    {
        add(cutoff,
            latency,
            altNames,
            peak,
            sidechain,
            seenTempo,
            seenPlaying,
            rateFraction);
    }

    RenamableParam cutoff {"Cutoff",
                           FloatRange::withCentre(20.f, 20000.f, 1000.f),
                           1000.f,
                           {.label = "Hz"}};
    IntParam latency {"Latency", 0, 1024, 0};
    BoolParam altNames {"Alt Names", false};
    FloatParam peak {"Peak", 0.f, 1.f, 0.f};
    FloatParam sidechain {"Sidechain", 0.f, 1.f, 0.f};
    FloatParam seenTempo {"Seen Tempo", 0.f, 1000.f, 0.f};
    BoolParam seenPlaying {"Seen Playing", false};
    FloatParam rateFraction {"Rate Fraction", 0.f, 1.f, 0.f};
};

float peakOf(const Buffer& buffer) noexcept
{
    auto peak = 0.f;

    for (auto ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (auto sample: buffer[ch])
            peak = std::max(peak, std::abs(sample));

    return peak;
}

struct EffectPlugin : StatePlugin<State<EffectParams>>
{
    std::string_view name() const override { return "HostFixtureEffect"; }

    BusLayout getBusLayout() const override
    {
        auto layout = BusLayout::stereoInOut();
        layout.inputs.add({.name = "Sidechain",
                           .numChannels = 1,
                           .isMain = false,
                           .activeByDefault = false});
        return layout;
    }

    int latencySamples() const noexcept override { return params.latency.get(); }

    // A wrapper loads the parameters and the rest apart, the rest on the message
    // thread, where a rename belongs.
    void loadState(std::string_view data, StateContext context) override
    {
        StatePlugin::loadState(data, context);
        applyNames();
    }

    void loadStateExceptParameters(std::string_view data,
                                   StateContext context) override
    {
        StatePlugin::loadStateExceptParameters(data, context);
        applyNames();
    }

    void applyNames()
    {
        auto alt = params.altNames.isOn();
        params.cutoff.setDisplayInfo(alt ? "Frequency" : "Cutoff", {}, "Hz");
        refreshParameterNames();
    }

    void prepare(const ProcessSpec& spec) override
    {
        rateFraction =
            static_cast<float>(spec.sampleRate - std::floor(spec.sampleRate));
    }

    void process(ProcessContext& context) noexcept override
    {
        const auto& input = context.mainInput();
        auto& output = context.mainOutput();

        if (!input.isEmpty()
            && input.getChannelPointers() != output.getChannelPointers())
            output.copyFrom(input);

        params.peak.setValue(peakOf(input));
        params.sidechain.setValue(
            context.inputs.size() > 1 ? peakOf(context.inputs[1]) : 0.f);
        params.seenTempo.setValue(static_cast<float>(context.playhead.bpm));
        params.seenPlaying.setValue(context.playhead.isPlaying ? 1.f : 0.f);
        params.rateFraction.setValue(rateFraction);
    }

    float rateFraction = 0.f;
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
