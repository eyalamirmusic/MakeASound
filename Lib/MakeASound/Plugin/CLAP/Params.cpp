#include "Params.h"
#include "Adapter.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace MakeASound::CLAP
{

namespace
{
// Whether the steps are exactly the whole numbers from minValue to maxValue.
bool hasWholeNumberSteps(const Parameter& param) noexcept
{
    auto steps = param.numSteps();
    auto low = param.minValue();

    return steps > 0 && std::nearbyint(low) == low
           && param.maxValue() - low == static_cast<float>(steps);
}

std::string modulePath(const ParameterList::Entry& entry)
{
    auto path = std::string {};

    for (const auto& group: entry.groups)
    {
        if (!path.empty())
            path += '/';

        path += group.name;
    }

    return path;
}

uint32_t paramCount(const clap_plugin* plugin) noexcept
{
    return static_cast<uint32_t>(Adapter::from(plugin)->paramCount());
}

bool paramInfo(const clap_plugin* plugin,
               uint32_t index,
               clap_param_info* info) noexcept
{
    return info != nullptr
           && Adapter::from(plugin)->paramInfo(static_cast<int>(index), *info);
}

bool paramValue(const clap_plugin* plugin, clap_id id, double* value) noexcept
{
    return value != nullptr && Adapter::from(plugin)->paramValue(id, *value);
}

bool valueToText(const clap_plugin* plugin,
                 clap_id id,
                 double value,
                 char* buffer,
                 uint32_t capacity) noexcept
{
    return buffer != nullptr && capacity > 0
           && Adapter::from(plugin)->valueToText(id, value, buffer, capacity);
}

bool textToValue(const clap_plugin* plugin,
                 clap_id id,
                 const char* text,
                 double* value) noexcept
{
    return text != nullptr && value != nullptr
           && Adapter::from(plugin)->textToValue(id, text, *value);
}

void flush(const clap_plugin* plugin,
           const clap_input_events* in,
           const clap_output_events* out) noexcept
{
    Adapter::from(plugin)->flush(in, out);
}

constexpr auto extension = clap_plugin_params {
    paramCount, paramInfo, paramValue, valueToText, textToValue, flush};
} // namespace

ValueMapping valueMappingFor(const Parameter& param, ClapValueSpace space) noexcept
{
    auto mapping = ValueMapping {};

    if (space == ClapValueSpace::JuceNormalized)
    {
        mapping.stepped = param.isBoolean();
        return mapping;
    }

    if (!hasWholeNumberSteps(param))
        return mapping;

    mapping.plain = true;
    mapping.stepped = true;
    mapping.isEnum = param.isDiscrete() && !param.isBoolean();
    mapping.minValue = param.minValue();
    mapping.maxValue = param.maxValue();
    return mapping;
}

double toPublished(const Parameter& param,
                   const ValueMapping& mapping,
                   float normalized) noexcept
{
    auto clamped = std::clamp(normalized, 0.f, 1.f);

    if (!mapping.plain)
        return static_cast<double>(clamped);

    return std::clamp(static_cast<double>(param.toPlain(clamped)),
                      mapping.minValue,
                      mapping.maxValue);
}

float toNormalized(const Parameter& param,
                   const ValueMapping& mapping,
                   double published) noexcept
{
    if (!std::isfinite(published))
        return param.getNormalized();

    auto clamped = std::clamp(published, mapping.minValue, mapping.maxValue);

    if (!mapping.plain)
        return static_cast<float>(clamped);

    return std::clamp(param.toNormalized(static_cast<float>(clamped)), 0.f, 1.f);
}

void fillParamInfo(const ParameterList& list,
                   int index,
                   const ValueMapping& mapping,
                   clap_param_info& info) noexcept
{
    const auto& entry = list.entry(index);
    const auto& param = *entry.param;

    info = {};
    info.id = entry.clapId;
    info.cookie = nullptr;

    if (entry.automatable)
        info.flags |= CLAP_PARAM_IS_AUTOMATABLE;

    if (mapping.stepped)
        info.flags |= CLAP_PARAM_IS_STEPPED;

    if (mapping.isEnum)
        info.flags |= CLAP_PARAM_IS_ENUM;

    if (entry.bypass)
        info.flags |= CLAP_PARAM_IS_BYPASS;

    info.min_value = mapping.minValue;
    info.max_value = mapping.maxValue;
    info.default_value =
        toPublished(param, mapping, param.toNormalized(param.defaultValue()));

    copyText(param.name(), info.name, CLAP_NAME_SIZE);

    try
    {
        copyText(modulePath(entry), info.module, CLAP_PATH_SIZE);
    }
    catch (...)
    {
        info.module[0] = 0;
    }
}

void copyText(std::string_view text, char* buffer, int capacity) noexcept
{
    if (buffer == nullptr || capacity <= 0)
        return;

    auto length = std::min(static_cast<int>(text.size()), capacity - 1);

    // Back off a continuation byte the cut would leave dangling.
    if (length < static_cast<int>(text.size()))
        while (
            length > 0
            && (static_cast<unsigned char>(text[static_cast<size_t>(length)]) & 0xc0)
                   == 0x80)
            --length;

    std::memcpy(buffer, text.data(), static_cast<size_t>(length));
    buffer[length] = 0;
}

const clap_plugin_params* paramsExtension() noexcept
{
    return &extension;
}

int Adapter::paramCount() const noexcept
{
    return listed.size();
}

bool Adapter::paramInfo(int position, clap_param_info& info) const noexcept
{
    if (position < 0 || position >= listed.size())
        return false;

    auto index = listed[position];
    fillParamInfo(pluginWrapper.plugin().parameters(), index, mappings[index], info);
    return true;
}

bool Adapter::paramValue(clap_id id, double& value) const noexcept
{
    auto index = indexOfClapId(id);

    if (index < 0)
        return false;

    const auto& param = pluginWrapper.plugin().parameters()[index];
    value = toPublished(
        param, mappings[index], pluginWrapper.getNormalizedParameter(index));
    return true;
}

bool Adapter::valueToText(clap_id id,
                          double value,
                          char* buffer,
                          uint32_t capacity) const noexcept
{
    auto index = indexOfClapId(id);

    if (index < 0)
        return false;

    const auto& param = pluginWrapper.plugin().parameters()[index];
    auto normalized = toNormalized(param, mappings[index], value);

    try
    {
        copyText(param.valueToText(param.toPlain(normalized)),
                 buffer,
                 static_cast<int>(std::min(capacity, uint32_t {1u << 30})));
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool Adapter::textToValue(clap_id id, const char* text, double& value) const noexcept
{
    auto index = indexOfClapId(id);

    if (index < 0)
        return false;

    const auto& param = pluginWrapper.plugin().parameters()[index];

    try
    {
        auto plain = param.textToValue(text);
        value = toPublished(param, mappings[index], param.toNormalized(plain));
        return true;
    }
    catch (...)
    {
        return false;
    }
}

int Adapter::indexOfClapId(clap_id id) const noexcept
{
    auto found = std::lower_bound(byClapId.begin(),
                                  byClapId.end(),
                                  id,
                                  [](const ClapSlot& slot, clap_id wanted)
                                  { return slot.id < wanted; });

    if (found == byClapId.end() || found->id != id)
        return -1;

    return found->index;
}

} // namespace MakeASound::CLAP
