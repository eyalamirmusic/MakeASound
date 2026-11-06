#include "../../Plugin/VST3/VST3Common.h"
#include "VST3HostedParameter.h"

#include <public.sdk/source/vst/utility/stringconvert.h>

#include <algorithm>
#include <cmath>

namespace MakeASound::Hosting
{

namespace
{
using namespace Steinberg;
using namespace Steinberg::Vst;

std::string text(const String128 value)
{
    return StringConvert::convert(value);
}

ParameterOptions optionsFor(const ParameterInfo& info, const std::string& id)
{
    auto options = ParameterOptions();
    options.id = id;
    options.automatable = (info.flags & ParameterInfo::kCanAutomate) != 0;
    options.bypass = (info.flags & ParameterInfo::kIsBypass) != 0;
    options.hostId = info.id;
    return options;
}

float clampUnit(float value) noexcept
{
    return std::isfinite(value) ? std::clamp(value, 0.f, 1.f) : 0.f;
}
} // namespace

// The id outlives the options only through Parameter's own copy, so it is made
// once here and handed over as a view.
VST3HostedParameter::VST3HostedParameter(IEditController& controllerToUse,
                                         const ParameterInfo& info,
                                         VST3ParameterSink& sinkToUse)
    : Parameter(text(info.title), optionsFor(info, std::to_string(info.id)))
    , controller(controllerToUse)
    , sink(sinkToUse)
    , vstId(info.id)
    , initial(clampUnit(static_cast<float>(info.defaultNormalizedValue)))
    , steps(std::max(0, static_cast<int>(info.stepCount)))
    , value(
          clampUnit(static_cast<float>(controllerToUse.getParamNormalized(info.id))))
{
    setDisplayInfo(name(), text(info.shortTitle), text(info.units));
}

void VST3HostedParameter::setValue(float normalized) noexcept
{
    if (!std::isfinite(normalized))
        return;

    auto clamped = std::clamp(normalized, 0.f, 1.f);
    value.store(clamped, std::memory_order_relaxed);
    sink.parameterEdited(vstId, clamped);
}

float VST3HostedParameter::getValue() const noexcept
{
    return value.load(std::memory_order_relaxed);
}

std::string VST3HostedParameter::valueToText(float normalized) const
{
    // Not `auto`: String128 is an array, which `auto` would decay to a pointer
    // into a temporary.
    String128 result = {};

    if (controller.getParamStringByValue(vstId, clampUnit(normalized), result)
        == kResultOk)
        return text(result);

    return Parameter::valueToText(normalized);
}

float VST3HostedParameter::textToValue(std::string_view input) const
{
    auto wide = StringConvert::convert(std::string(input));
    auto normalized = ParamValue {};

    if (controller.getParamValueByString(
            vstId, const_cast<TChar*>(wide.data()), normalized)
        == kResultOk)
        return clampUnit(static_cast<float>(normalized));

    return Parameter::textToValue(input);
}

void VST3HostedParameter::store(float normalized) noexcept
{
    value.store(clampUnit(normalized), std::memory_order_relaxed);
}

bool VST3HostedParameter::updateInfo(const ParameterInfo& info)
{
    if (info.id != vstId)
        return false;

    auto title = text(info.title);
    auto shortTitle = text(info.shortTitle);
    auto units = text(info.units);

    if (title == name() && shortTitle == shortName() && units == label())
        return false;

    setDisplayInfo(title, shortTitle, units);
    return true;
}

void VST3HostedParameter::markChangedByProcessor() noexcept
{
    changedByProcessor.store(true, std::memory_order_release);
}

bool VST3HostedParameter::takeChangedByProcessor() noexcept
{
    return changedByProcessor.exchange(false, std::memory_order_acq_rel);
}

} // namespace MakeASound::Hosting
