#include "HostParameters.h"
#include "../Compat/JuceIdentity.h"
#include "../Core/IdentityPolicy.h"

#include <algorithm>
#include <cmath>

namespace MakeASound::AU
{

namespace
{
// One plain value per step, each a whole number: what an AU host takes Indexed
// to mean.
bool isIndexed(const Parameter& param) noexcept
{
    auto steps = param.numSteps();
    auto low = param.minValue();

    return steps > 0 && low == std::round(low)
           && param.maxValue() - low == static_cast<float>(steps);
}

AudioUnitParameterOptions displayFlag(Parameter::Curve curve) noexcept
{
    switch (curve)
    {
        case Parameter::Curve::SquareRoot:
            return kAudioUnitParameterFlag_DisplaySquareRoot;
        case Parameter::Curve::Squared:
            return kAudioUnitParameterFlag_DisplaySquared;
        case Parameter::Curve::Cubed:
            return kAudioUnitParameterFlag_DisplayCubed;
        case Parameter::Curve::CubeRoot:
            return kAudioUnitParameterFlag_DisplayCubeRoot;
        case Parameter::Curve::Linear:
        case Parameter::Curve::Other:
            break;
    }

    return 0;
}
} // namespace

CFStringRef makeCFString(std::string_view text)
{
    return CFStringCreateWithBytes(kCFAllocatorDefault,
                                   reinterpret_cast<const UInt8*>(text.data()),
                                   static_cast<CFIndex>(text.size()),
                                   kCFStringEncodingUTF8,
                                   false);
}

std::string toUtf8(CFStringRef text)
{
    if (text == nullptr)
        return {};

    auto range = CFRangeMake(0, CFStringGetLength(text));
    auto size = CFIndex {};
    CFStringGetBytes(
        text, range, kCFStringEncodingUTF8, '?', false, nullptr, 0, &size);

    auto result = std::string(static_cast<size_t>(size), '\0');
    CFStringGetBytes(text,
                     range,
                     kCFStringEncodingUTF8,
                     '?',
                     false,
                     reinterpret_cast<UInt8*>(result.data()),
                     size,
                     nullptr);
    return result;
}

void fillParameterInfo(const ParameterList::Entry& entry,
                       AudioUnitParameterInfo& info)
{
    const auto& param = *entry.param;

    info = AudioUnitParameterInfo {};
    info.cfNameString = makeCFString(entry.displayName);
    CFStringGetCString(
        info.cfNameString, info.name, sizeof(info.name), kCFStringEncodingUTF8);

    auto normalized = publishesNormalized(param);

    info.minValue = normalized ? 0.f : param.minValue();
    info.maxValue = normalized ? 1.f : param.maxValue();
    info.defaultValue = toHostValue(param, param.defaultValue());

    if (param.stateFormat() == Parameter::StateFormat::Bool)
        info.unit = kAudioUnitParameterUnit_Boolean;
    else if (isIndexed(param) && !normalized)
        info.unit = kAudioUnitParameterUnit_Indexed;
    else
        info.unit = kAudioUnitParameterUnit_Generic;

    info.flags =
        kAudioUnitParameterFlag_IsReadable | kAudioUnitParameterFlag_IsWritable
        | kAudioUnitParameterFlag_HasCFNameString
        | kAudioUnitParameterFlag_CFNameRelease
        | kAudioUnitParameterFlag_IsHighResolution | displayFlag(param.curve());

    if (hasValueStrings(param))
        info.flags |= kAudioUnitParameterFlag_ValuesHaveStrings;
}

bool publishesNormalized(const Parameter& param) noexcept
{
    return param.curve() == Parameter::Curve::Other;
}

float toHostValue(const Parameter& param, float plain) noexcept
{
    return publishesNormalized(param) ? param.toNormalized(plain) : plain;
}

float fromHostValue(const Parameter& param, float hostValue) noexcept
{
    return publishesNormalized(param) ? param.toPlain(hostValue) : hostValue;
}

Vector<int> parameterListOrder(const ParameterList& list)
{
    return ParameterSpace {}.listOrder(list);
}

int firstUnhintedParameter(const ParameterList& list, int skip) noexcept
{
    return ParameterSpace {}.firstUnhinted(list, skip);
}

bool hasValueStrings(const Parameter& param) noexcept
{
    auto format = param.stateFormat();
    return param.numSteps() > 0
           && (format == Parameter::StateFormat::Text
               || format == Parameter::StateFormat::Bool);
}

CFArrayRef copyValueStrings(const Parameter& param)
{
    if (!hasValueStrings(param))
        return nullptr;

    auto steps = param.numSteps();
    auto* array =
        CFArrayCreateMutable(kCFAllocatorDefault, steps + 1, &kCFTypeArrayCallBacks);

    for (auto step = 0; step <= steps; ++step)
    {
        auto plain =
            param.toPlain(static_cast<float>(step) / static_cast<float>(steps));
        auto* text = copyTextForValue(param, plain);
        CFArrayAppendValue(array, text);
        CFRelease(text);
    }

    return array;
}

CFStringRef copyTextForValue(const Parameter& param, float plain)
{
    return makeCFString(param.valueToText(plain));
}

float valueForText(const Parameter& param, CFStringRef text)
{
    auto value = param.textToValue(toUtf8(text));

    if (!std::isfinite(value))
        return param.getValue();

    return std::clamp(value, param.minValue(), param.maxValue());
}

ParameterSpace::ParameterSpace(const ParameterIdentity& identity) noexcept
    : rules(identity)
{
}

ParameterSpace::ParameterSpace(IdScheme scheme, const JuceSettings& juce) noexcept
    : ParameterSpace(policyFor(scheme, juce).parameters)
{
}

ParameterSpace::ParameterSpace(const PluginDescription& plugin) noexcept
    : ParameterSpace(identityPolicyFor(plugin).parameters)
{
}

bool ParameterSpace::isListed(const ParameterList& list, int index) const noexcept
{
    auto registered = rules.registration == Registration::All
                          ? index >= 0 && index < list.size()
                          : list.isHostExposed(index);

    return registered && list.entry(index).listedInAU;
}

Compat::JuceAUParameterInfo
    ParameterSpace::juceInfo(const ParameterList::Entry& entry) const noexcept
{
    auto settings = JuceSettings {};
    settings.forceLegacyParameterAutomationType = rules.juceLegacyAutomationType;

    auto scheme = rules.paramIds == ParamIdRule::Index ? IdScheme::JUCELegacyIndex
                                                       : IdScheme::JUCE;

    return Compat::juceAUParameterInfo(
        *entry.param, entry.automatable, scheme, settings);
}

Vector<int> ParameterSpace::listOrder(const ParameterList& list) const
{
    auto order = Vector<int> {};

    for (auto index = 0; index < list.size(); ++index)
        if (isListed(list, index))
            order.add(index);

    auto hostId = [&](int index) { return list.entry(index).hostId; };
    auto hint = [&](int index) { return list[index].versionHint(); };
    auto postPort = [&](int index) { return list.entry(index).postPort; };

    // An index profile's indexed entries by index, which is declaration order
    // (the SDK's indexed list, ids 0..n-1, or Plug's ascending sparse ids); any
    // post-port entries follow, by (hint, id).
    if (rules.auOrder != AUOrder::HintThenId)
    {
        auto firstPostPort = std::stable_partition(
            order.begin(), order.end(), [&](int index) { return !postPort(index); });
        std::sort(firstPostPort,
                  order.end(),
                  [&](int a, int b) { return hostId(a) < hostId(b); });
        std::stable_sort(firstPostPort,
                         order.end(),
                         [&](int a, int b) { return hint(a) < hint(b); });
        return order;
    }

    std::sort(order.begin(),
              order.end(),
              [&](int a, int b) { return hostId(a) < hostId(b); });
    std::stable_sort(
        order.begin(), order.end(), [&](int a, int b) { return hint(a) < hint(b); });
    return order;
}

bool ParameterSpace::usesSdkIndexedList(const ParameterList& list) const noexcept
{
    if (rules.auOrder != AUOrder::SdkIndexed)
        return false;

    for (auto index = 0; index < list.size(); ++index)
        if (isListed(list, index) && list.entry(index).postPort)
            return false;

    return true;
}

int ParameterSpace::firstUnhinted(const ParameterList& list, int skip) const noexcept
{
    for (auto index = 0; index < list.size(); ++index)
        if (index != skip && isListed(list, index) && list[index].versionHint() == 0)
            return index;

    return -1;
}

void ParameterSpace::fillInfo(const ParameterList::Entry& entry,
                              AudioUnitParameterInfo& info) const
{
    const auto& param = *entry.param;

    info = AudioUnitParameterInfo {};
    info.cfNameString = makeCFString(entry.displayName);
    CFStringGetCString(
        info.cfNameString, info.name, sizeof(info.name), kCFStringEncodingUTF8);

    // The range, by the value space.
    auto plainRange = true;

    switch (rules.auValues)
    {
        case AUValueSpace::NativeCurve:
            plainRange = !publishesNormalized(param);
            info.minValue = plainRange ? param.minValue() : 0.f;
            info.maxValue = plainRange ? param.maxValue() : 1.f;
            break;

        case AUValueSpace::JuceScaled:
        {
            auto juce = juceInfo(entry);
            plainRange = false;
            info.minValue = juce.minValue;
            info.maxValue = juce.maxValue;
            break;
        }

        case AUValueSpace::Plain:
            info.minValue = param.minValue();
            info.maxValue = param.maxValue();
            break;
    }

    info.defaultValue = toHost(entry, param.defaultValue());

    constexpr auto always = kAudioUnitParameterFlag_IsReadable
                            | kAudioUnitParameterFlag_IsWritable
                            | kAudioUnitParameterFlag_HasCFNameString
                            | kAudioUnitParameterFlag_CFNameRelease;

    // The unit and flags, by the units.
    switch (rules.auUnits)
    {
        case AUUnits::Native:
            if (param.stateFormat() == Parameter::StateFormat::Bool)
                info.unit = kAudioUnitParameterUnit_Boolean;
            else if (isIndexed(param) && plainRange)
                info.unit = kAudioUnitParameterUnit_Indexed;
            else
                info.unit = kAudioUnitParameterUnit_Generic;

            info.flags = always | kAudioUnitParameterFlag_IsHighResolution
                         | displayFlag(param.curve());

            if (AU::hasValueStrings(param))
                info.flags |= kAudioUnitParameterFlag_ValuesHaveStrings;

            break;

        case AUUnits::Juce:
        {
            // AU_1.mm GetParameterInfo, less meters, which MakeASound has
            // not, and clumps, which addClump adds for the v2 unit.
            auto juce = juceInfo(entry);

            if (juce.indexedUnit)
                info.unit = kAudioUnitParameterUnit_Indexed;
            else if (juce.boolean && juce.discrete
                     && !rules.juceLegacyAutomationType)
                info.unit = kAudioUnitParameterUnit_Boolean;
            else
                info.unit = kAudioUnitParameterUnit_Generic;

            info.flags = always | kAudioUnitParameterFlag_ValuesHaveStrings;

            if (juce.highResolution)
                info.flags |= kAudioUnitParameterFlag_IsHighResolution;

            if (juce.nonRealTime)
                info.flags |= kAudioUnitParameterFlag_NonRealTime;

            if (juce.canRamp)
                info.flags |= kAudioUnitParameterFlag_CanRamp;

            break;
        }

        case AUUnits::Plug:
            // Plug's AU/Adapter.cpp GetParameterInfo.
            info.unit = param.numSteps() > 0 ? kAudioUnitParameterUnit_Indexed
                                             : kAudioUnitParameterUnit_Generic;
            info.flags = always;
            break;
    }
}

float ParameterSpace::toHost(const ParameterList::Entry& entry,
                             float plain) const noexcept
{
    const auto& param = *entry.param;

    if (rules.auValues == AUValueSpace::NativeCurve)
        return toHostValue(param, plain);

    if (rules.auValues == AUValueSpace::Plain)
        return plain;

    auto juce = juceInfo(entry);
    return juce.toHost(param.toNormalized(plain));
}

float ParameterSpace::fromHost(const ParameterList::Entry& entry,
                               float hostValue) const noexcept
{
    const auto& param = *entry.param;

    if (rules.auValues == AUValueSpace::NativeCurve)
        return fromHostValue(param, hostValue);

    if (rules.auValues == AUValueSpace::Plain)
        return hostValue;

    auto juce = juceInfo(entry);
    return param.toPlain(std::clamp(juce.toNormalized(hostValue), 0.f, 1.f));
}

bool ParameterSpace::hasValueStrings(const ParameterList::Entry& entry,
                                     AUFlavour flavour) const noexcept
{
    if (rules.auUnits == AUUnits::Native)
        return AU::hasValueStrings(*entry.param);

    // Plug's AU answers no ParameterValueStrings.
    if (rules.auUnits == AUUnits::Plug)
        return false;

    auto juce = juceInfo(entry);
    return flavour == AUFlavour::V2 ? juce.v2ValueStrings : juce.v3ValueStrings;
}

CFArrayRef ParameterSpace::copyValueStrings(const ParameterList::Entry& entry,
                                            AUFlavour flavour) const
{
    const auto& param = *entry.param;

    if (!hasValueStrings(entry, flavour))
        return nullptr;

    if (rules.auUnits == AUUnits::Native)
        return AU::copyValueStrings(param);

    // JUCE's getNumSteps() strings, the text of i / maxValue each; past 1 under
    // the legacy automation type, which JUCE's range clamps.
    auto juce = juceInfo(entry);
    auto count = param.numSteps() + 1;
    auto* array =
        CFArrayCreateMutable(kCFAllocatorDefault, count, &kCFTypeArrayCallBacks);

    for (auto step = 0; step < count; ++step)
    {
        auto normalized = std::min(static_cast<float>(step) / juce.maxValue, 1.f);
        auto* text = copyTextForValue(param, param.toPlain(normalized));
        CFArrayAppendValue(array, text);
        CFRelease(text);
    }

    return array;
}

UInt32 ParameterSpace::clumpIdFor(const ParameterList::Entry& entry) const noexcept
{
    if (!rules.hostGroups || entry.groups.empty())
        return 0;

    return static_cast<UInt32>(entry.groups.back().index + 1);
}

void ParameterSpace::addClump(const ParameterList::Entry& entry,
                              AudioUnitParameterInfo& info) const noexcept
{
    if (auto clump = clumpIdFor(entry); clump != 0)
    {
        info.flags |= kAudioUnitParameterFlag_HasClump;
        info.clumpID = clump;
    }
}

CFStringRef ParameterSpace::copyClumpName(const ParameterList& list,
                                          UInt32 clumpId) const
{
    const auto& groups = list.groups();

    if (!rules.hostGroups || clumpId == 0
        || clumpId > static_cast<UInt32>(groups.size()))
        return nullptr;

    return makeCFString(groups[static_cast<int>(clumpId) - 1].clumpName);
}

} // namespace MakeASound::AU
