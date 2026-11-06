#include "SynthPlugin.h"

namespace MakeASoundExamples
{

void SynthPlugin::prepare(const ProcessSpec& spec)
{
    synth.setSettings(settingsFromParams());
    synth.prepare(spec);
}

void SynthPlugin::reset() noexcept
{
    synth.reset();
}

void SynthPlugin::process(ProcessContext& context) noexcept
{
    synth.setSettings(settingsFromParams());
    synth.process(context);
}

DSP::TestSynth::Settings SynthPlugin::settingsFromParams() const noexcept
{
    return {.waveform = static_cast<DSP::Waveform>(params.waveform.getIndex()),
            .attackSeconds = params.attack.get(),
            .releaseSeconds = params.release.get(),
            .gain = params.level.gain(),
            .legato = params.legato.isOn()};
}

} // namespace MakeASoundExamples

namespace MakeASound
{

ModuleDescription describeModule()
{
    auto module = ModuleDescription {};
    module.vendor = "MakeASound";
    module.manufacturerCode = "MkAS";
    module.plugins.add(
        {.name = "Synth",
         .category = Category::Instrument,
         .pluginCode = "Synt",
         .create = [] { return EA::makeOwned<MakeASoundExamples::SynthPlugin>(); }});
    return module;
}

} // namespace MakeASound
