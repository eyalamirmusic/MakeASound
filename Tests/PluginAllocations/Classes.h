#pragma once

// The suites makeasound_add_allocation_tests builds into a plugin's own
// executable (CMake/MakeASoundAllocationTests.cmake) run every driver over every
// class of the one describeModule() it links, the plugin's. A failure names the
// class it was measuring.

#include <MakeASound/Plugin/MakeASoundPlugin.h>

#include <string>
#include <string_view>

namespace PluginAllocations
{

inline const MakeASound::ModuleDescription& module()
{
    static const auto described = MakeASound::describeModule();
    return described;
}

// `fn(description, label)` for each class, the label "<name> ('<code>')".
template <typename Fn>
void forEachClass(Fn&& fn)
{
    for (const auto& plugin: module().plugins)
    {
        auto label = std::string(plugin.name) + " ('"
                     + std::string(plugin.pluginCode.chars, 4) + "')";
        fn(plugin, std::string_view(label));
    }
}

} // namespace PluginAllocations
