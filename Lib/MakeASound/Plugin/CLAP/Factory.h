#pragma once

#include "../Core/Description.h"

#include <clap/factory/plugin-factory.h>

#include <memory>
#include <string>
#include <string_view>

namespace MakeASound::CLAP
{

// The clap.plugin-factory over every plugin in `module`, which must outlive it:
// one descriptor per class, its id clapIdFor(bundleId, plugin) (Core/Identity.h),
// its features from the category. create_plugin makes an Adapter over the
// description's create() under its parameterIdentityFor().
class Factory
{
public:
    Factory(const ModuleDescription& module, std::string_view bundleId);
    ~Factory();

    Factory(const Factory&) = delete;
    Factory& operator=(const Factory&) = delete;

    const clap_plugin_factory* get() const noexcept { return &table.factory; }

    int size() const noexcept;
    const clap_plugin_descriptor* descriptor(int index) const noexcept;

    // Null for an unknown id, a host of an incompatible CLAP version, or a
    // plugin that failed to construct.
    const clap_plugin* create(const clap_host* host,
                              const char* pluginId) const noexcept;

    // The C table first, so the factory pointer a callback gets leads back to
    // its owner.
    struct Table
    {
        clap_plugin_factory factory {};
        const Factory* owner = nullptr;
    };

private:
    struct Class;

    Table table;
    Vector<OwningPointer<Class>> classes;
};

// The bundle id makeasound_add_plugin gave this module (MAKEASOUND_CLAP_BUNDLE_ID),
// defined by the module's EntryPoint.cpp, or by a test binary of its own.
std::string_view moduleBundleId() noexcept;

} // namespace MakeASound::CLAP
