#pragma once

#include "PluginDescription.h"

#include <string_view>

namespace MakeASound::Hosting
{

// Free-text filtering of a scanned plugin list: what a host's search box does,
// kept out of the UI so it can be tested without one.
//
// The query is split on whitespace and EVERY token must appear, ignoring case, in
// one of the fields a user types: the name, the vendor, the sub-categories or the
// path. Tokens narrow rather than widen ("arturia synth" finds that vendor's
// instruments), and a blank query matches everything.
inline bool matchesFilter(const PluginDescription& description,
                          std::string_view query)
{
    auto matchesToken = [&description](std::string_view token)
    {
        return Text::containsIgnoringCase(description.name, token)
               || Text::containsIgnoringCase(description.vendor, token)
               || Text::containsIgnoringCase(description.subCategories, token)
               || Text::containsIgnoringCase(description.path, token);
    };

    auto isSpace = [](char c) { return c == ' ' || c == '\t' || c == '\n'; };
    auto position = std::string_view::size_type {0};

    while (position < query.size())
    {
        while (position < query.size() && isSpace(query[position]))
            ++position;

        auto end = position;

        while (end < query.size() && !isSpace(query[end]))
            ++end;

        if (end > position && !matchesToken(query.substr(position, end - position)))
            return false;

        position = end;
    }

    return true;
}

// The descriptions matching `query`, in the order they came in.
inline Vector<PluginDescription>
    filterPlugins(const Vector<PluginDescription>& plugins, std::string_view query)
{
    auto result = Vector<PluginDescription>();

    for (const auto& description: plugins)
        if (matchesFilter(description, query))
            result.add(description);

    return result;
}

} // namespace MakeASound::Hosting
