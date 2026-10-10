#include "GainPlugin.h"

namespace MakeASoundExamples
{

void GainPlugin::prepare(const ProcessSpec& spec)
{
    gain.setSampleRate(spec.sampleRate);
    gain.setRampTime(0.02f);
    reset();
}

void GainPlugin::reset() noexcept
{ gain.reset(params.gain.gain()); }

void GainPlugin::process(ProcessContext& context) noexcept
{
    gain.setTarget(params.gain.gain());
    gain.applyGain(context.mainOutput());
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
        {.name = "Gain",
         .category = Category::Effect,
         .pluginCode = "Gain",
         .create = [] { return EA::makeOwned<MakeASoundExamples::GainPlugin>(); }});
    return module;
}

} // namespace MakeASound
