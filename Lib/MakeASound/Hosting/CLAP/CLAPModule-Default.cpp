#include "CLAPModule.h"

#include <clap/entry.h>

#include <eacp/Core/Plugins/DynamicLibrary.h>
#include <eacp/Core/Utils/FilePath.h>

#include <utility>

namespace MakeASound::Hosting
{

// The shared library itself, loaded with local binding, so the module's own
// copies of everything stay its own.
struct CLAPModule::Library
{
    eacp::Plugins::DynamicLibrary image;
};

CLAPModule::Library* CLAPModule::openLibrary(const std::string& path,
                                             std::string& error)
{
    auto library = EA::makeOwned<Library>();

    if (!library->image.open(eacp::FilePath(path)))
    {
        error = library->image.lastError();
        return nullptr;
    }

    return library.release();
}

const clap_plugin_entry* CLAPModule::entryOf(const Library& library)
{
    return static_cast<const clap_plugin_entry*>(
        library.image.findSymbol("clap_entry"));
}

// The entry is deinit()ed by now, so the unload has nothing left to quiesce;
// eacp defers the unmap to a later loop turn when a loop is running.
void CLAPModule::closeLibrary(Library* library)
{
    if (library == nullptr)
        return;

    eacp::Plugins::unload(std::move(library->image));
    delete library;
}

} // namespace MakeASound::Hosting
