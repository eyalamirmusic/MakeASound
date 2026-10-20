#pragma once

// describeIdentity: every id a host stores for a plugin and its parameters, one
// stable line each, so a golden copy (Tests/Identity/) catches any change to a
// shipped identity. Each line starts with a tag: `plugin` and `core` lines come
// from the SDK-free core, the rest from a format, whose TU adds its section only
// where that format is built (IdentityManifestVST3.cpp, -AU, -AAX). A golden's
// lines of a format this build lacks are left out of the comparison.

#include <MakeASound/Plugin/MakeASoundPlugin.h>

#include <eacp/Core/Utils/Format.h>

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

// One format's part: where its lines go among the others' (the core is 0), the
// tags its lines start with, and what writes them.
struct Section
{
    int order = 0;
    std::vector<std::string_view> tags;
    DescribeFn describe;
};

// Every section built into this binary, by order, the core's first.
std::vector<Section>& sections();

// For a format TU's namespace-scope initialiser. It keeps sections() in order,
// because the order format TUs initialise in is unspecified (a unity build or
// the linker can change it).
bool addSection(Section section);

// The plugin's lines from every section, each ending in a newline.
std::string describeIdentity(const ModuleDescription& module,
                             const PluginDescription& plugin);

// Whether a line's tag is one a built section writes.
bool isDescribedHere(std::string_view line);

// A brace pattern ("{:08x}") into a std::string, through eacp's formatter.
template <typename... Args>
std::string print(std::string_view pattern, const Args&... args)
{
    return eacp::Strings::format(pattern, args...);
}

std::string fourCC(uint32_t code);

} // namespace IdentityManifest
