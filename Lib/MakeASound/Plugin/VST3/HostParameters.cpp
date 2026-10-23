#include "VST3Common.h"
#include "HostParameters.h"
#include "Text.h"
#include "../Compat/JuceIdentity.h"

#include "pluginterfaces/vst/ivstmidicontrollers.h"

namespace MakeASound::VST3
{

std::optional<ShadowTarget> decodeShadowId(const Vst3Controllers& controllers,
                                           Vst::ParamID id,
                                           int numMidiInputs) noexcept
{
    if (controllers.layout == Vst3ControllerLayout::JuceContiguous)
    {
        auto offset = id - controllers.base;

        if (numMidiInputs < 1 || offset >= 16u * juceShadowControllers)
            return std::nullopt;

        return ShadowTarget {
            .bus = 0,
            .channel = static_cast<int>(offset / juceShadowControllers),
            .controller = static_cast<int>(offset % juceShadowControllers)};
    }

    if ((id & 0xffff0000u) != controllers.base)
        return std::nullopt;

    auto target = ShadowTarget {.bus = static_cast<int>((id >> 12) & 0xf),
                                .channel = static_cast<int>((id >> 8) & 0xf),
                                .controller = static_cast<int>(id & 0xff)};

    if (target.controller >= shadowControllers || target.bus >= numMidiInputs)
        return std::nullopt;

    return target;
}

std::optional<ShadowTarget> decodeShadowId(Vst::ParamID id,
                                           int numMidiInputs) noexcept
{
    return decodeShadowId(Vst3Controllers {}, id, numMidiInputs);
}

std::string shadowName(const Vst3Controllers& controllers,
                       const ShadowTarget& target,
                       int numMidiInputs)
{
    if (controllers.layout == Vst3ControllerLayout::JuceContiguous)
        return "MIDI CC " + std::to_string(target.channel) + "|"
               + std::to_string(target.controller);

    return shadowName(target, numMidiInputs);
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

Vst::UnitID unitIdFor(const ParameterList& list,
                      const ParameterList::Entry& entry) noexcept
{
    if (!list.identity().hostGroups || entry.groups.empty())
        return Vst::kRootUnitId;

    return static_cast<Vst::UnitID>(list.groups()[entry.groups.back().index].unitId);
}

Vector<UnitDescription> unitsFor(const ParameterList& list)
{
    auto units = Vector<UnitDescription> {};

    if (!list.identity().hostGroups)
        return units;

    units.add({Vst::kRootUnitId, Vst::kNoParentUnitId, "Root Unit"});

    const auto& groups = list.groups();
    auto used = Vector<int> {};
    used.resize(groups.size(), 0);

    for (auto i = 0; i < list.size(); ++i)
        if (list.isRegistered(i))
            for (const auto& group: list.entry(i).groups)
                used[group.index] = 1;

    for (auto i = 0; i < groups.size(); ++i)
    {
        if (used[i] == 0)
            continue;

        const auto& group = groups[i];
        auto parent = group.parent >= 0
                          ? static_cast<Vst::UnitID>(groups[group.parent].unitId)
                          : Vst::kRootUnitId;
        units.add({static_cast<Vst::UnitID>(group.unitId), parent, group.name});
    }

    return units;
}

ProxyParameter::ProxyParameter(const ParameterList::Entry& entryToUse,
                               const ParameterIdentity& identity,
                               Vst::UnitID unitToUse)
    : entry(entryToUse)
    , unit(unitToUse)
    , steps(identity.steps)
    , juceLegacyAutomationType(identity.juceLegacyAutomationType)
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
    info.stepCount = steps == StepRule::JuceDiscrete
                         ? Compat::juceVST3StepCount(p, juceLegacyAutomationType)
                         : p.numSteps();
    info.defaultNormalizedValue = p.toNormalized(p.defaultValue());
    info.unitId = unit;
    // Only a JUCE scheme registers a non-automatable entry, published without
    // kCanAutomate as JUCE's wrapper publishes one.
    info.flags = entry.automatable ? Vst::ParameterInfo::kCanAutomate : 0;

    if (entry.bypass)
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
