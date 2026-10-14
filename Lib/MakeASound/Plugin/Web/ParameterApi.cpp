#include "ParameterApi.h"
#include "../Parameters/ChoiceParam.h"

namespace MakeASound::Web
{

ParameterApi::ParameterApi(Plugin& pluginToUse)
    : plugin(&pluginToUse)
{
    auto& list = plugin->parameters();

    for (auto i = 0; i < list.size(); ++i)
        slots.add({.published = list[i].getNormalized()});
}

WebParameters ParameterApi::getParameters()
{
    auto result = WebParameters {};

    if (plugin == nullptr)
        return result;

    auto& list = plugin->parameters();

    for (auto i = 0; i < list.size(); ++i)
    {
        auto& entry = list.entry(i);
        auto& param = list[i];
        auto value = valueOf(i);
        auto fallback = param.defaultValue();

        auto parameter =
            WebParameter {.id = entry.id,
                          .key = param.id(),
                          .name = entry.displayName,
                          .label = param.label(),
                          .normalized = value.normalized,
                          .text = value.text,
                          .defaultNormalized = param.toNormalized(fallback),
                          .defaultText = param.valueToText(fallback),
                          .steps = param.numSteps(),
                          .automatable = entry.automatable};

        for (const auto& group: entry.groups)
            parameter.groups.add(group.ownId);

        if (auto* choice = dynamic_cast<const ChoiceParam*>(&param))
            for (auto c = 0; c < choice->numChoices(); ++c)
                parameter.choices.add(choice->choiceName(c));

        // A bool's two labels, off then on, read through its own valueToText(),
        // as Tamber Plug's page schema sends them.
        if (param.isBoolean())
            for (auto normalized: {0.f, 1.f})
                parameter.choices.add(param.valueToText(param.toPlain(normalized)));

        slots[i].published = param.getNormalized();
        result.parameters.add(std::move(parameter));
    }

    return result;
}

WebParameterSnapshot ParameterApi::getParameterValues()
{
    if (plugin == nullptr)
        return {};

    auto& list = plugin->parameters();

    for (auto i = 0; i < list.size(); ++i)
        slots[i].published = list[i].getNormalized();

    return snapshot();
}

void ParameterApi::beginEdit(const WebParameterId& param)
{
    auto index = indexOf(param.id);

    if (index >= 0 && !slots[index].editing)
        begin(index);
}

void ParameterApi::edit(const WebParameterEdit& change)
{
    auto index = indexOf(change.id);

    if (index >= 0)
        perform(index, static_cast<float>(change.normalized));
}

void ParameterApi::endEdit(const WebParameterId& param)
{
    auto index = indexOf(param.id);

    if (index >= 0 && slots[index].editing)
        end(index);
}

void ParameterApi::editText(const WebParameterText& entry)
{
    auto index = indexOf(entry.id);

    if (index < 0)
        return;

    auto& param = plugin->parameters()[index];
    perform(index, param.toNormalized(param.textToValue(entry.text)));
}

void ParameterApi::resetParam(const WebParameterId& param)
{
    auto index = indexOf(param.id);

    if (index < 0)
        return;

    auto& parameter = plugin->parameters()[index];
    perform(index, parameter.toNormalized(parameter.defaultValue()));
}

void ParameterApi::resetAll()
{
    if (plugin == nullptr)
        return;

    auto& list = plugin->parameters();

    for (auto i = 0; i < list.size(); ++i)
    {
        if (i == list.bypassIndex())
            continue;

        auto& param = list[i];
        auto normalized = param.toNormalized(param.defaultValue());

        if (param.getNormalized() != normalized)
            perform(i, normalized);
    }
}

void ParameterApi::endAllEdits()
{
    for (auto i = 0; i < slots.size(); ++i)
        if (slots[i].editing)
            end(i);
}

void ParameterApi::poll()
{
    if (plugin == nullptr)
        return;

    auto moved = WebParameterValues {};
    auto& list = plugin->parameters();

    for (auto i = 0; i < list.size(); ++i)
    {
        auto normalized = list[i].getNormalized();

        if (normalized == slots[i].published)
            continue;

        slots[i].published = normalized;
        moved.changed.add(valueOf(i));
    }

    if (moved.changed.empty())
        return;

    values.publish(std::move(moved));
    parameterValues.publish(snapshot());
}

int ParameterApi::indexOf(std::string_view id) const
{
    if (plugin == nullptr)
        return -1;

    return plugin->parameters().indexOf(id);
}

WebParameterValue ParameterApi::valueOf(int index) const
{
    auto& entry = plugin->parameters().entry(index);
    auto& param = *entry.param;

    return {.id = entry.id,
            .normalized = param.getNormalized(),
            .text = param.valueToText(param.getValue())};
}

WebParameterSnapshot ParameterApi::snapshot() const
{
    auto result = WebParameterSnapshot {};
    auto& list = plugin->parameters();

    for (auto i = 0; i < list.size(); ++i)
        result.items.add(valueOf(i));

    return result;
}

void ParameterApi::perform(int index, float normalized)
{
    auto whole = !slots[index].editing;

    if (whole)
        begin(index);

    auto& param = plugin->parameters()[index];
    param.setNormalized(normalized);

    if (auto* listener = plugin->hostEditListener())
        listener->performParameterEdit(index, param.getNormalized());

    if (whole)
        end(index);
}

void ParameterApi::begin(int index)
{
    slots[index].editing = true;

    if (auto* listener = plugin->hostEditListener())
        listener->beginParameterEdit(index);
}

void ParameterApi::end(int index)
{
    slots[index].editing = false;

    if (auto* listener = plugin->hostEditListener())
        listener->endParameterEdit(index);
}

} // namespace MakeASound::Web
