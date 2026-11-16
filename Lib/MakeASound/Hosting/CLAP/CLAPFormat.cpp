#include "CLAPFormat.h"
#include "CLAPHost.h"
#include "CLAPHostedPlugin.h"
#include "CLAPModule.h"
#include "../Scan/Scanner.h"

#include <clap/ext/audio-ports.h>
#include <clap/ext/gui.h>
#include <clap/ext/note-ports.h>
#include <clap/factory/plugin-factory.h>
#include <clap/plugin-features.h>
#include <clap/plugin.h>
#include <clap/version.h>

#include <cstring>

namespace MakeASound::Hosting
{

namespace
{
std::string clapText(const char* text)
{
    return text == nullptr ? std::string() : std::string(text);
}

bool hasFeature(const clap_plugin_descriptor& descriptor, const char* feature)
{
    if (descriptor.features == nullptr)
        return false;

    for (auto* const* each = descriptor.features; *each != nullptr; ++each)
        if (std::strcmp(*each, feature) == 0)
            return true;

    return false;
}

// The features as they are, '|'-separated like VST3's sub-categories, so
// PluginDescription::isInstrument() finds "instrument" in them.
std::string joinedFeatures(const clap_plugin_descriptor& descriptor)
{
    auto joined = std::string();

    if (descriptor.features == nullptr)
        return joined;

    for (auto* const* each = descriptor.features; *each != nullptr; ++each)
    {
        if (**each == '\0')
            continue;

        if (!joined.empty())
            joined += '|';

        joined += *each;
    }

    return joined;
}

// The spec's three main kinds, an instrument first: a synth that also processes
// its input is still found among instruments.
std::string categoryOf(const clap_plugin_descriptor& descriptor)
{
    if (hasFeature(descriptor, CLAP_PLUGIN_FEATURE_INSTRUMENT))
        return "Instrument";

    if (hasFeature(descriptor, CLAP_PLUGIN_FEATURE_NOTE_EFFECT)
        && !hasFeature(descriptor, CLAP_PLUGIN_FEATURE_AUDIO_EFFECT))
        return "Note Effect";

    return "Audio Effect";
}

int countPortChannels(const clap_plugin& plugin,
                      const clap_plugin_audio_ports& ports,
                      bool isInput)
{
    auto total = 0;
    const auto count = ports.count(&plugin, isInput);

    for (auto i = uint32_t {0}; i < count; ++i)
    {
        auto info = clap_audio_port_info();

        if (ports.get(&plugin, i, isInput, &info))
            total += static_cast<int>(info.channel_count);
    }

    return total;
}

template <typename Extension>
const Extension* extensionOf(const clap_plugin& plugin, const char* id)
{
    if (plugin.get_extension == nullptr)
        return nullptr;

    return static_cast<const Extension*>(plugin.get_extension(&plugin, id));
}

// Creates one plugin just long enough to ask about its ports and gui: init()ed,
// never activated, so it processes nothing and opens no window.
void describePlugin(const clap_plugin_factory& factory,
                    const CLAPHost& host,
                    const clap_plugin_descriptor& descriptor,
                    PluginDescription& description)
{
    const auto* plugin = factory.create_plugin(&factory, host.get(), descriptor.id);

    if (plugin == nullptr)
        return;

    if (plugin->init != nullptr && plugin->init(plugin))
    {
        using AudioPorts = clap_plugin_audio_ports;
        using NotePorts = clap_plugin_note_ports;

        if (const auto* ports =
                extensionOf<AudioPorts>(*plugin, CLAP_EXT_AUDIO_PORTS))
        {
            description.numAudioInputs = countPortChannels(*plugin, *ports, true);
            description.numAudioOutputs = countPortChannels(*plugin, *ports, false);
        }

        if (const auto* notes = extensionOf<NotePorts>(*plugin, CLAP_EXT_NOTE_PORTS))
            description.hasMidiInput = notes->count(plugin, true) > 0;

        // Offering the gui extension at all is how a CLAP plugin says it has
        // an editor; which window API it embeds in is asked when it opens.
        description.hasEditor =
            extensionOf<clap_plugin_gui>(*plugin, CLAP_EXT_GUI) != nullptr;
    }

    // Valid after a failed init too, which the spec allows.
    if (plugin->destroy != nullptr)
        plugin->destroy(plugin);
}
} // namespace

PluginFormat CLAPFormat::format() const
{
    return PluginFormat::CLAP;
}

Vector<std::string> CLAPFormat::findTargets(const Vector<std::string>& folders) const
{
    return findModulesWithExtension(folders, ".clap");
}

bool CLAPFormat::ownsTarget(std::string_view target) const
{
    return Text::endsWithIgnoringCase(target, ".clap");
}

ModuleScan CLAPFormat::scan(std::string_view target) const
{
    auto scan = ModuleScan();
    scan.format = PluginFormat::CLAP;
    scan.path = std::string(target);
    scan.setStamp(stamp(target));

    auto error = std::string();
    auto module = openModule(scan.path, error);

    if (!module)
    {
        scan.error = error;
        return scan;
    }

    // A module with no plugin factory (one that only offers presets, say) is
    // not a failure: it caches as a good "nothing here", as VST3 does.
    const auto* factory = static_cast<const clap_plugin_factory*>(
        module->getFactory(CLAP_PLUGIN_FACTORY_ID));

    if (factory == nullptr || factory->get_plugin_count == nullptr
        || factory->get_plugin_descriptor == nullptr
        || factory->create_plugin == nullptr)
        return scan;

    // Outlives every plugin created below.
    const auto host = CLAPHost(HostInfo().name);
    const auto count = factory->get_plugin_count(factory);

    for (auto i = uint32_t {0}; i < count; ++i)
    {
        const auto* descriptor = factory->get_plugin_descriptor(factory, i);

        if (descriptor == nullptr || descriptor->id == nullptr
            || !clap_version_is_compatible(descriptor->clap_version))
            continue;

        auto description = PluginDescription();
        description.format = PluginFormat::CLAP;
        description.path = scan.path;
        description.classId = descriptor->id;
        description.name = clapText(descriptor->name);
        description.vendor = clapText(descriptor->vendor);
        description.version = clapText(descriptor->version);
        description.category = categoryOf(*descriptor);
        description.subCategories = joinedFeatures(*descriptor);

        describePlugin(*factory, host, *descriptor, description);

        scan.classes.add(std::move(description));
    }

    return scan;
}

LoadResult CLAPFormat::load(const PluginDescription& description,
                            const HostInfo& host) const
{
    auto failure = [](std::string error)
    {
        auto result = LoadResult();
        result.error = std::move(error);
        return result;
    };

    auto error = std::string();
    auto module = openModule(description.path, error);

    if (!module)
        return failure(error.empty() ? "the module failed to load" : error);

    const auto* factory = static_cast<const clap_plugin_factory*>(
        module->getFactory(CLAP_PLUGIN_FACTORY_ID));

    if (factory == nullptr || factory->get_plugin_count == nullptr
        || factory->get_plugin_descriptor == nullptr
        || factory->create_plugin == nullptr)
        return failure("the module has no plugin factory");

    // By id, never position: the class a selection names survives an update
    // that reorders the module's plugins.
    const clap_plugin_descriptor* chosen = nullptr;
    const auto count = factory->get_plugin_count(factory);

    for (auto i = uint32_t {0}; i < count && chosen == nullptr; ++i)
    {
        const auto* descriptor = factory->get_plugin_descriptor(factory, i);

        if (descriptor != nullptr && descriptor->id != nullptr
            && clap_version_is_compatible(descriptor->clap_version)
            && description.classId == descriptor->id)
            chosen = descriptor;
    }

    if (chosen == nullptr)
        return failure("the module has no plugin with id " + description.classId);

    // The id outlives the call: the descriptor is the module's, and the module
    // stays loaded as long as the plugin.
    const auto* id = chosen->id;

    return CLAPHostedPlugin::create(
        description,
        std::move(module),
        [factory, id](const clap_host* clapHost)
        { return factory->create_plugin(factory, clapHost, id); },
        host);
}

std::shared_ptr<CLAPModule> CLAPFormat::openModule(std::string_view path,
                                                   std::string& error) const
{
    // Held across the load: two threads asking for one path get one init().
    const auto lock = std::scoped_lock(modulesLock);

    if (auto found = modules.find(path); found != modules.end())
    {
        if (auto alive = found->second.lock())
            return alive;

        modules.erase(found);
    }

    // Whatever else let go meanwhile is dropped too, so the map stays small.
    std::erase_if(modules, [](const auto& entry) { return entry.second.expired(); });

    auto module = CLAPModule::load(path, error);

    if (module)
        modules.emplace(std::string(path), module);

    return module;
}

} // namespace MakeASound::Hosting
