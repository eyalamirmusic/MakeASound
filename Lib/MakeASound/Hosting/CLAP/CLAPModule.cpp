#include "CLAPModule.h"

#include <clap/entry.h>
#include <clap/version.h>

namespace MakeASound::Hosting
{

namespace
{
std::string versionText(const clap_version& version)
{
    return std::to_string(version.major) + "." + std::to_string(version.minor) + "."
           + std::to_string(version.revision);
}
} // namespace

std::shared_ptr<CLAPModule> CLAPModule::load(std::string_view path,
                                             std::string& error)
{
    const auto pathText = std::string(path);
    auto* library = openLibrary(pathText, error);

    if (library == nullptr)
    {
        if (error.empty())
            error = "the module failed to load";

        return {};
    }

    // Owns the library from here, so every early return below closes it; the
    // entry is set only once init() succeeded, so only then is it deinit()ed.
    auto module = std::shared_ptr<CLAPModule>(new CLAPModule());
    module->library = library;

    const auto* entry = entryOf(*library);

    if (entry == nullptr)
    {
        error = "the module exports no clap_entry";
        return {};
    }

    if (!clap_version_is_compatible(entry->clap_version))
    {
        error = "the module speaks CLAP " + versionText(entry->clap_version)
                + ", which this host does not";
        return {};
    }

    if (entry->init == nullptr || entry->get_factory == nullptr
        || !entry->init(pathText.c_str()))
    {
        error = "the module's clap_entry did not initialise";
        return {};
    }

    module->entry = entry;
    return module;
}

CLAPModule::~CLAPModule()
{
    if (entry != nullptr && entry->deinit != nullptr)
        entry->deinit();

    closeLibrary(library);
}

const void* CLAPModule::getFactory(const char* id) const
{
    if (entry == nullptr || id == nullptr)
        return nullptr;

    return entry->get_factory(id);
}

} // namespace MakeASound::Hosting
