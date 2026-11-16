#include "CLAPModule.h"

#include <clap/entry.h>

#include <CoreFoundation/CoreFoundation.h>

namespace MakeASound::Hosting
{

// The bundle, its executable loaded. Releasing it never unloads the image: a
// CFBundle's executable stays mapped for the process once loaded, which is what
// macOS hosts do, since a plugin's Objective-C classes and queued blocks can
// outlive its deinit.
struct CLAPModule::Library
{
    CFBundleRef bundle = nullptr;
};

namespace
{
std::string utf8Of(CFStringRef text)
{
    if (text == nullptr)
        return {};

    const auto length = CFStringGetLength(text);
    const auto capacity =
        CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
    auto buffer = std::string(static_cast<size_t>(capacity), '\0');

    if (!CFStringGetCString(text, buffer.data(), capacity, kCFStringEncodingUTF8))
        return {};

    buffer.resize(std::char_traits<char>::length(buffer.c_str()));
    return buffer;
}

std::string textOfError(CFErrorRef error)
{
    if (error == nullptr)
        return {};

    auto* description = CFErrorCopyDescription(error);
    auto text = utf8Of(description);

    if (description != nullptr)
        CFRelease(description);

    return text;
}
} // namespace

CLAPModule::Library* CLAPModule::openLibrary(const std::string& path,
                                             std::string& error)
{
    auto* url = CFURLCreateFromFileSystemRepresentation(
        kCFAllocatorDefault,
        reinterpret_cast<const UInt8*>(path.data()),
        static_cast<CFIndex>(path.size()),
        true);

    if (url == nullptr)
    {
        error = "the module's path is not a valid file URL";
        return nullptr;
    }

    auto* bundle = CFBundleCreate(kCFAllocatorDefault, url);
    CFRelease(url);

    if (bundle == nullptr)
    {
        error = "the module is not a bundle";
        return nullptr;
    }

    auto loadError = CFErrorRef();

    if (!CFBundleLoadExecutableAndReturnError(bundle, &loadError))
    {
        error = textOfError(loadError);

        if (loadError != nullptr)
            CFRelease(loadError);

        if (error.empty())
            error = "the module's executable failed to load";

        CFRelease(bundle);
        return nullptr;
    }

    return new Library {bundle};
}

const clap_plugin_entry* CLAPModule::entryOf(const Library& library)
{
    // clap_entry is data, a const struct, not a function.
    return static_cast<const clap_plugin_entry*>(
        CFBundleGetDataPointerForName(library.bundle, CFSTR("clap_entry")));
}

void CLAPModule::closeLibrary(Library* library)
{
    if (library == nullptr)
        return;

    CFRelease(library->bundle);
    delete library;
}

} // namespace MakeASound::Hosting
