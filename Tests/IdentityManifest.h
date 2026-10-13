#pragma once

// describeIdentity: every id a host stores for a plugin and its parameters, one
// stable line each, so a golden copy (Tests/Identity/) catches any change to a
// shipped identity. Each line starts with a tag: `plugin` and `core` lines come
// from the SDK-free core, the rest from a format, whose TU adds its section only
// where that format is built (IdentityManifestVST3.cpp, -AU, -AAX). A golden's
// lines of a format this build lacks are left out of the comparison.

#include <MakeASound/Plugin/MakeASoundPlugin.h>

#include <cstdio>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace IdentityManifest
{
using namespace MakeASound;

using Lines = std::vector<std::string>;

using DescribeFn = std::function<void(
    const ModuleDescription& module, const PluginDescription& plugin, Lines& out)>;

// One format's part: the tags its lines start with, and what writes them.
struct Section
{
    std::vector<std::string_view> tags;
    DescribeFn describe;
};

// Every section built into this binary, the core's first.
std::vector<Section>& sections();

// For a format TU's namespace-scope initialiser.
bool addSection(Section section);

// The plugin's lines from every section, each ending in a newline.
std::string describeIdentity(const ModuleDescription& module,
                             const PluginDescription& plugin);

// Whether a line's tag is one a built section writes.
bool isDescribedHere(std::string_view line);

// printf into a std::string.
template <typename... Args>
std::string print(const char* format, Args... args)
{
    auto size = std::snprintf(nullptr, 0, format, args...);
    auto text = std::string(static_cast<size_t>(size), '\0');
    std::snprintf(text.data(), text.size() + 1, format, args...);
    return text;
}

std::string fourCC(uint32_t code);

} // namespace IdentityManifest
