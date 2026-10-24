#include "ApvtsXml.h"
#include "JuceState.h"

#include <unordered_map>

namespace MakeASound::Compat
{

namespace
{
// The value setNewState gives a parameter from its PARAM: the `value`, or the
// default when the PARAM has none.
float plainValueOf(const JuceTree& param, const Parameter& target)
{
    const auto* value = param.property("value");
    return value != nullptr ? static_cast<float>(juceStringToDouble(*value))
                            : target.defaultValue();
}

void applyParams(const JuceTree& root,
                 ParameterGroup& params,
                 const ApvtsOptions& options)
{
    auto byId = std::unordered_map<std::string, const JuceTree*> {};

    for (const auto& child: root.children)
        if (const auto* id = child.property("id");
            child.type == "PARAM" && id != nullptr)
            byId[*id] = &child;

    params.forEach(
        [&](Parameter& param,
            std::string_view idPath,
            std::string_view,
            bool,
            bool persistent,
            std::span<const ParameterGroup* const>)
        {
            if (!persistent)
                return;

            auto juceId = options.mapId ? options.mapId(param, idPath) : param.id();

            if (auto found = byId.find(juceId); found != byId.end())
                param.setValue(plainValueOf(*found->second, param));
            else if (options.missing == MissingPolicy::Default)
                param.resetToDefault();
        });
}
} // namespace

bool loadApvtsTree(const JuceTree& root,
                   ParameterGroup& params,
                   const ApvtsOptions& options)
{
    if (!options.rootType.empty() && root.type != options.rootType)
        return false;

    try
    {
        applyParams(root, params, options);
        return true;
    }
    catch (...)
    {
        // Only a throwing mapId gets here, part way through.
        return false;
    }
}

std::optional<JuceTree> loadApvtsXml(std::string_view xml,
                                     ParameterGroup& params,
                                     const ApvtsOptions& options)
{
    auto root = parseJuceXml(xml);

    if (!root || !loadApvtsTree(*root, params, options))
        return std::nullopt;

    return root;
}

std::optional<JuceTree> loadJuceApvtsBlob(std::string_view bytes,
                                          ParameterGroup& params,
                                          const ApvtsOptions& options)
{
    if (isJuceXmlBlob(bytes))
    {
        auto text = juceXmlText(bytes);
        return text ? loadApvtsXml(*text, params, options) : std::nullopt;
    }

    auto root = readJuceValueTree(bytes);

    if (!root || !loadApvtsTree(*root, params, options))
        return std::nullopt;

    return root;
}

} // namespace MakeASound::Compat
