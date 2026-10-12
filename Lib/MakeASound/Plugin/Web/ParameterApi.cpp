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

        auto parameter = WebParameter {.id = entry.id,
                                       .name = entry.displayName,
                                       .label = param.label(),
                                       .normalized = value.normalized,
                                       .text = value.text,
                                       .steps = param.numSteps(),
                                       .automatable = entry.automatable};

        if (auto* choice = dynamic_cast<const ChoiceParam*>(&param))
            for (auto c = 0; c < choice->numChoices(); ++c)
                parameter.choices.add(choice->choiceName(c));

        slots[i].published = param.getNormalized();
        result.parameters.add(std::move(parameter));
    }

    return result;
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

    if (index < 0)
        return;

    auto whole = !slots[index].editing;

    if (whole)
        begin(index);

    auto& param = plugin->parameters()[index];
    param.setNormalized(static_cast<float>(change.normalized));

    if (auto* listener = plugin->hostEditListener())
        listener->performParameterEdit(index, param.getNormalized());

    if (whole)
        end(index);
}

void ParameterApi::endEdit(const WebParameterId& param)
{
    auto index = indexOf(param.id);

    if (index >= 0 && slots[index].editing)
        end(index);
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

    if (!moved.changed.empty())
        values.publish(std::move(moved));
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
