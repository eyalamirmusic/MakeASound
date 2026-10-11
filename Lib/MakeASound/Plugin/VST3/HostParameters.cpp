#include "VST3Common.h"
#include "HostParameters.h"
#include "Text.h"

#include "pluginterfaces/vst/ivstmidicontrollers.h"

namespace MakeASound::VST3
{

std::optional<ShadowTarget> decodeShadowId(Vst::ParamID id,
                                           int numMidiInputs) noexcept
{
    if ((id & 0xffff0000u) != shadowIdBase)
        return std::nullopt;

    auto target = ShadowTarget {.bus = static_cast<int>((id >> 12) & 0xf),
                                .channel = static_cast<int>((id >> 8) & 0xf),
                                .controller = static_cast<int>(id & 0xff)};

    if (target.controller >= shadowControllers || target.bus >= numMidiInputs)
        return std::nullopt;

    return target;
}

std::string shadowName(const ShadowTarget& target, int numMidiInputs)
{
    auto name = std::string {};

    switch (target.controller)
    {
        case Vst::kAfterTouch:
            name = "Aftertouch";
            break;
        case Vst::kPitchBend:
            name = "Pitch Bend";
            break;
        case Vst::kCtrlProgramChange:
            name = "Program Change";
            break;
        default:
            name = "MIDI CC " + std::to_string(target.controller);
    }

    name += " Ch " + std::to_string(target.channel + 1);

    if (numMidiInputs > 1)
        name += " Bus " + std::to_string(target.bus + 1);

    return name;
}

ProxyParameter::ProxyParameter(const ParameterList::Entry& entryToUse)
    : entry(entryToUse)
{
    refreshInfo();
    valueNormalized = info.defaultNormalizedValue;
}

void ProxyParameter::refreshInfo()
{
    const auto& p = param();

    info.id = entry.hostId;
    copyTo(info.title, entry.displayName);
    copyTo(info.shortTitle, p.shortName());
    copyTo(info.units, p.label());
    info.stepCount = p.numSteps();
    info.defaultNormalizedValue = p.toNormalized(p.defaultValue());
    info.unitId = Vst::kRootUnitId;
    info.flags = Vst::ParameterInfo::kCanAutomate;

    if (p.isBypass())
        info.flags |= Vst::ParameterInfo::kIsBypass;

    if (p.stateFormat() == MakeASound::Parameter::StateFormat::Text)
        info.flags |= Vst::ParameterInfo::kIsList;
}

void ProxyParameter::toString(Vst::ParamValue normalized, Vst::String128 text) const
{
    const auto& p = param();
    copyTo(text, p.valueToText(p.toPlain(static_cast<float>(normalized))));
}

bool ProxyParameter::fromString(const Vst::TChar* text,
                                Vst::ParamValue& normalized) const
{
    const auto& p = param();
    normalized = p.toNormalized(p.textToValue(toUtf8(text)));
    return true;
}

Vst::ParamValue ProxyParameter::toPlain(Vst::ParamValue normalized) const
{
    return param().toPlain(static_cast<float>(normalized));
}

Vst::ParamValue ProxyParameter::toNormalized(Vst::ParamValue plain) const
{
    return param().toNormalized(static_cast<float>(plain));
}

} // namespace MakeASound::VST3
