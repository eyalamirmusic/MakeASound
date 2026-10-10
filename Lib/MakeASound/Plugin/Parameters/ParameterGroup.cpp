#include "ParameterGroup.h"
#include "../State/StateContext.h"

#include <cmath>
#include <cstdlib>

namespace MakeASound
{
namespace
{
std::string join(const std::string& prefix, std::string_view name, char separator)
{
    auto joined = prefix;

    if (!joined.empty() && !name.empty())
        joined += separator;

    joined += name;
    return joined;
}

Miro::Options slotOptions(const Miro::Reflector& ref, Miro::Shape shape)
{
    auto options = ref.options();
    options.shape = shape;
    options.nullable = false;
    options.omittable = false;
    return options;
}

void saveParameter(Miro::Reflector& ref, const Parameter& param)
{
    switch (param.stateFormat())
    {
        case Parameter::StateFormat::Text:
        {
            auto text = param.toStateText();
            ref.visit(text);
            return;
        }

        case Parameter::StateFormat::Bool:
        {
            auto on = param.getValue() >= 0.5f;
            ref.visit(on);
            return;
        }

        case Parameter::StateFormat::Number:
            break;
    }

    auto number = std::strtod(param.toStateText().c_str(), nullptr);
    ref.visit(number);
}

bool loadParameter(Miro::Reflector& ref, Parameter& param)
{
    switch (ref.kind())
    {
        case Miro::ValueKind::String:
        {
            auto text = std::string {};
            ref.visit(text);
            return param.fromStateText(text);
        }

        case Miro::ValueKind::Number:
        {
            auto number = 0.0;
            ref.visit(number);

            if (!std::isfinite(number))
                return false;

            param.setValue(static_cast<float>(number));
            return true;
        }

        case Miro::ValueKind::Bool:
        {
            auto on = false;
            ref.visit(on);
            param.setValue(on ? 1.f : 0.f);
            return true;
        }

        default:
            return false;
    }
}
} // namespace

ParameterGroup::ParameterGroup(std::string_view nameToUse, GroupOptions options)
    : groupName(nameToUse)
    , groupId(options.id.empty() ? nameToUse : options.id)
    , automatable(options.automatable)
{
}

void ParameterGroup::addParameter(Parameter& param)
{
    assert(!hasKey(param.id()) && "two members of a group share an id");
    entries.add(Entry {&param, nullptr});
}

void ParameterGroup::addGroup(ParameterGroup& group)
{
    assert(&group != this && "a group cannot hold itself");
    assert((group.id().empty() || !hasKey(group.id()))
           && "two members of a group share an id");
    entries.add(Entry {nullptr, &group});
}

bool ParameterGroup::hasKey(std::string_view key) const
{
    for (const auto& entry: entries)
    {
        if (entry.param != nullptr)
        {
            if (entry.param->id() == key)
                return true;
        }
        else if (entry.group->id().empty() ? entry.group->hasKey(key)
                                           : entry.group->id() == key)
        {
            return true;
        }
    }

    return false;
}

void ParameterGroup::forEach(const Visitor& visitor) const
{ walk(visitor, {}, {}, true); }

void ParameterGroup::walk(const Visitor& visitor,
                          const std::string& idPrefix,
                          const std::string& displayPrefix,
                          bool inheritedAutomatable) const
{
    auto groupAutomatable = inheritedAutomatable && automatable;

    for (const auto& entry: entries)
    {
        if (entry.param != nullptr)
        {
            auto& param = *entry.param;
            visitor(param,
                    join(idPrefix, param.id(), '/'),
                    join(displayPrefix, param.name(), ' '),
                    groupAutomatable && param.isAutomatable());
            continue;
        }

        auto& group = *entry.group;
        group.walk(visitor,
                   join(idPrefix, group.id(), '/'),
                   join(displayPrefix, group.name(), ' '),
                   groupAutomatable);
    }
}

void ParameterGroup::reflect(Miro::Reflector& ref)
{
    // A load handed no object says nothing, so it moves nothing.
    if (ref.isLoading() && ref.kind() != Miro::ValueKind::Object)
        return;

    reflectEntries(ref, isSession(ref));
}

void ParameterGroup::reflectEntries(Miro::Reflector& ref, bool session)
{
    for (const auto& entry: entries)
    {
        if (entry.param != nullptr)
        {
            auto& param = *entry.param;

            if (param.isSessionOnly() && !session)
                continue;

            auto& slot =
                ref.atKey(param.id(), slotOptions(ref, Miro::Shape::Primitive));

            if (ref.isSaving())
                saveParameter(slot, param);
            else if (!loadParameter(slot, param))
                param.resetToDefault();

            continue;
        }

        auto& group = *entry.group;

        if (group.id().empty())
        {
            group.reflectEntries(ref, session);
            continue;
        }

        auto& slot = ref.atKey(group.id(), slotOptions(ref, Miro::Shape::Object));

        if (ref.isLoading() && slot.kind() != Miro::ValueKind::Object)
            group.resetToDefaults(session);
        else
            group.reflectEntries(slot, session);
    }
}

void ParameterGroup::resetToDefaults(bool session) noexcept
{
    for (const auto& entry: entries)
    {
        if (entry.group != nullptr)
            entry.group->resetToDefaults(session);
        else if (session || !entry.param->isSessionOnly())
            entry.param->resetToDefault();
    }
}

} // namespace MakeASound
