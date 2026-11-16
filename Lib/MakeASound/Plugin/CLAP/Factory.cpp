#include "Factory.h"
#include "Adapter.h"
#include "../Core/Identity.h"
#include "../Realtime/MessageThread.h"

#include <clap/plugin-features.h>
#include <clap/version.h>

namespace MakeASound::CLAP
{

struct Factory::Class
{
    std::string id;
    std::string name;
    std::string vendor;
    std::string url;
    std::string version;
    Vector<const char*> features;
    clap_plugin_descriptor descriptor {};
    const PluginDescription* plugin = nullptr;
};

namespace
{
const Factory& ownerOf(const clap_plugin_factory* factory) noexcept
{
    return *reinterpret_cast<const Factory::Table*>(factory)->owner;
}

uint32_t pluginCount(const clap_plugin_factory* factory) noexcept
{
    return static_cast<uint32_t>(ownerOf(factory).size());
}

const clap_plugin_descriptor* pluginDescriptor(const clap_plugin_factory* factory,
                                               uint32_t index) noexcept
{
    return ownerOf(factory).descriptor(static_cast<int>(index));
}

const clap_plugin* createPlugin(const clap_plugin_factory* factory,
                                const clap_host* host,
                                const char* pluginId) noexcept
{
    return ownerOf(factory).create(host, pluginId);
}

// The CLAP feature strings a category implies; the rest stay the author's.
void addFeatures(Vector<const char*>& features, Category category)
{
    switch (category)
    {
        case Category::Instrument:
            features.add(CLAP_PLUGIN_FEATURE_INSTRUMENT);
            features.add(CLAP_PLUGIN_FEATURE_SYNTHESIZER);
            break;

        case Category::MidiEffect:
            features.add(CLAP_PLUGIN_FEATURE_NOTE_EFFECT);
            break;

        case Category::Effect:
            features.add(CLAP_PLUGIN_FEATURE_AUDIO_EFFECT);
            break;
    }

    features.add(nullptr);
}
} // namespace

Factory::Factory(const ModuleDescription& module, std::string_view bundleId)
{
    table.factory = {pluginCount, pluginDescriptor, createPlugin};
    table.owner = this;

    for (const auto& plugin: module.plugins)
    {
        auto& entry = classes.add(EA::makeOwned<Class>());
        entry->id = clapIdFor(bundleId, plugin);
        entry->name = std::string(plugin.name);
        entry->vendor = std::string(module.vendor);
        entry->url = std::string(module.url);
        entry->version = std::string(plugin.version);
        entry->plugin = &plugin;
        addFeatures(entry->features, plugin.category);

        auto& descriptor = entry->descriptor;
        descriptor.clap_version = CLAP_VERSION;
        descriptor.id = entry->id.c_str();
        descriptor.name = entry->name.c_str();
        descriptor.vendor = entry->vendor.c_str();
        descriptor.url = entry->url.c_str();
        descriptor.manual_url = "";
        descriptor.support_url = "";
        descriptor.version = entry->version.c_str();
        descriptor.description = "";
        descriptor.features = entry->features.data();
    }
}

Factory::~Factory() = default;

int Factory::size() const noexcept
{
    return classes.size();
}

const clap_plugin_descriptor* Factory::descriptor(int index) const noexcept
{
    if (index < 0 || index >= classes.size())
        return nullptr;

    return &classes[index]->descriptor;
}

const clap_plugin* Factory::create(const clap_host* host,
                                   const char* pluginId) const noexcept
{
    if (host == nullptr || pluginId == nullptr
        || !clap_version_is_compatible(host->clap_version))
        return nullptr;

    for (const auto& entry: classes)
    {
        if (entry->id != pluginId)
            continue;

        try
        {
            adoptHostMessageThread();
            setCurrentPluginFormat(PluginFormat::CLAP);
            auto plugin = entry->plugin->create();

            if (plugin == nullptr)
                return nullptr;

            auto* adapter = new Adapter(host,
                                        &entry->descriptor,
                                        std::move(plugin),
                                        parameterIdentityFor(*entry->plugin));
            return adapter->clapPlugin();
        }
        catch (...)
        {
            return nullptr;
        }
    }

    return nullptr;
}

} // namespace MakeASound::CLAP
