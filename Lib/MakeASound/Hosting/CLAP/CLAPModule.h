#pragma once

#include <memory>
#include <string>
#include <string_view>

struct clap_plugin_entry;

namespace MakeASound::Hosting
{

// One loaded CLAP module, its entry init()ed: the RAII owner of the image and of
// the entry's init/deinit pair. Shared, so whatever is created from the module
// (a scan's plugins now, a hosted plugin later) keeps it, and the entry is
// deinit()ed exactly once, when the last owner lets go, before the image is let
// go. Message thread.
//
// Loading is per platform (CLAPModule-<os>.cpp): on macOS through CFBundle, the
// module being a bundle; elsewhere through eacp's DynamicLibrary. Either way
// the path the entry is init()ed with is the target's own: the bundle on macOS,
// the file elsewhere, as the spec has it.
class CLAPModule
{
public:
    // The module at `path`, its entry init()ed, or null with `error` saying why:
    // it would not load, exports no clap_entry, speaks an incompatible CLAP
    // version, or its init() refused.
    static std::shared_ptr<CLAPModule> load(std::string_view path,
                                            std::string& error);

    ~CLAPModule();

    CLAPModule(const CLAPModule&) = delete;
    CLAPModule& operator=(const CLAPModule&) = delete;

    // The entry's get_factory(id), or null.
    const void* getFactory(const char* id) const;

private:
    // The loaded image, defined per platform.
    struct Library;

    CLAPModule() = default;

    static Library* openLibrary(const std::string& path, std::string& error);
    static const clap_plugin_entry* entryOf(const Library& library);
    static void closeLibrary(Library* library);

    Library* library = nullptr;
    const clap_plugin_entry* entry = nullptr;
};

} // namespace MakeASound::Hosting
