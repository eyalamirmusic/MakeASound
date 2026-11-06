#include "AUValidator.h"

#include <eacp/Core/Process/Process.h>
#include <eacp/Core/Utils/StdPath.h>

#include <CoreFoundation/CoreFoundation.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <type_traits>

namespace MakeASound::Pluginval
{
namespace
{
struct CFReleaser
{
    void operator()(CFTypeRef ref) const { CFRelease(ref); }
};

template <typename T>
using CFOwned = std::unique_ptr<std::remove_pointer_t<T>, CFReleaser>;

struct Component
{
    std::string type;
    std::string subtype;
    std::string manufacturer;
    std::string name;
};

std::string toString(CFTypeRef value)
{
    if (value == nullptr || CFGetTypeID(value) != CFStringGetTypeID())
        return {};

    auto string = static_cast<CFStringRef>(value);
    auto length = CFStringGetLength(string);
    auto size = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
    auto buffer = std::string(static_cast<size_t>(size), '\0');

    if (!CFStringGetCString(string, buffer.data(), size, kCFStringEncodingUTF8))
        return {};

    buffer.resize(std::char_traits<char>::length(buffer.c_str()));
    return buffer;
}

Vector<Component> readComponents(const std::filesystem::path& bundle)
{
    auto components = Vector<Component> {};
    auto text = bundle.string();

    auto url = CFOwned<CFURLRef> {CFURLCreateFromFileSystemRepresentation(
        nullptr,
        reinterpret_cast<const UInt8*>(text.c_str()),
        static_cast<CFIndex>(text.size()),
        true)};

    if (!url)
        return components;

    // Read straight from the URL: CFBundleCreate caches by path, so a bundle
    // rebuilt in place would show the plist of the first one this process saw.
    auto info =
        CFOwned<CFDictionaryRef> {CFBundleCopyInfoDictionaryForURL(url.get())};

    if (!info)
        return components;

    auto entries = CFDictionaryGetValue(info.get(), CFSTR("AudioComponents"));

    if (entries == nullptr || CFGetTypeID(entries) != CFArrayGetTypeID())
        return components;

    auto array = static_cast<CFArrayRef>(entries);

    for (auto i = CFIndex {0}; i < CFArrayGetCount(array); ++i)
    {
        auto entry = CFArrayGetValueAtIndex(array, i);

        if (CFGetTypeID(entry) != CFDictionaryGetTypeID())
            continue;

        auto dictionary = static_cast<CFDictionaryRef>(entry);
        auto field = [&](CFStringRef key)
        { return toString(CFDictionaryGetValue(dictionary, key)); };

        components.push_back({.type = field(CFSTR("type")),
                              .subtype = field(CFSTR("subtype")),
                              .manufacturer = field(CFSTR("manufacturer")),
                              .name = field(CFSTR("name"))});
    }

    return components;
}

std::string install(const std::filesystem::path& bundle,
                    std::filesystem::path& installed)
{
    namespace fs = std::filesystem;

    auto folder = eacp::toStdPath(eacp::FilePath::homeDirectory()) / "Library"
                  / "Audio" / "Plug-Ins" / "Components";
    installed = folder / bundle.filename();

    auto created = std::error_code {};
    fs::create_directories(folder, created);

    if (created)
        return "could not create " + folder.string() + ": " + created.message();

    auto removed = std::error_code {};
    fs::remove_all(installed, removed);

    if (removed)
        return "could not remove " + installed.string() + ": " + removed.message();

    auto copied = std::error_code {};
    fs::copy(bundle,
             installed,
             fs::copy_options::recursive | fs::copy_options::copy_symlinks,
             copied);

    if (copied)
        return "could not copy into " + installed.string() + ": " + copied.message();

    return {};
}

eacp::Processes::ProcessResult runAuval(const Component& component,
                                        const Options& options)
{
    auto arguments = Vector<std::string> {
        "-strict", "-v", component.type, component.subtype, component.manufacturer};

    if (options.stress > 0)
    {
        arguments.emplace_back("-stress");
        arguments.emplace_back(std::to_string(options.stress));
    }

    return eacp::Processes::run("/usr/bin/auval", arguments);
}

bool notFound(const eacp::Processes::ProcessResult& run)
{
    auto marker = std::string_view {"didn't find the component"};
    return run.output.find(marker) != std::string::npos
           || run.errorOutput.find(marker) != std::string::npos;
}

bool succeeded(const eacp::Processes::ProcessResult& run)
{
    return run.launched && run.exited && run.exitCode == 0
           && run.output.find("AU VALIDATION SUCCEEDED") != std::string::npos;
}
} // namespace

Result validateAudioUnit(const eacp::FilePath& bundle, const Options& options)
{
    auto started = std::chrono::steady_clock::now();
    auto result = Result {.bundle = bundle};

    auto finish = [&]
    {
        auto elapsed = std::chrono::steady_clock::now() - started;
        result.seconds = std::chrono::duration<double>(elapsed).count();
        return result;
    };

    auto source = eacp::toStdPath(bundle);

    if (!source.has_filename())
        source = source.parent_path();

    auto components = readComponents(source);

    if (components.empty())
    {
        result.log =
            "no AudioComponents in " + source.string() + "/Contents/Info.plist\n";
        return finish();
    }

    auto installed = std::filesystem::path {};

    if (auto error = install(source, installed); !error.empty())
    {
        result.log = error + "\n";
        return finish();
    }

    result.log = "installed " + installed.string() + "\n";
    result.passed = true;
    result.exitCode = 0;
    auto registrarKilled = false;

    for (const auto& component: components)
    {
        result.log += "auval -strict " + component.type + " " + component.subtype
                      + " " + component.manufacturer + " (" + component.name + ")\n";

        auto run = runAuval(component, options);

        if (notFound(run) && !registrarKilled)
        {
            registrarKilled = true;
            result.log += "not found: restarting AudioComponentRegistrar\n";
            eacp::Processes::run("/usr/bin/killall",
                                 {"-9", "AudioComponentRegistrar"});
            run = runAuval(component, options);
        }

        result.log += run.output + run.errorOutput;

        if (!run.launched)
            result.log += "could not launch /usr/bin/auval\n";

        if (succeeded(run))
            continue;

        // The first failure's exit code, and 1 for an auval that exited 0 without
        // reporting success.
        if (result.passed)
            result.exitCode = run.exited && run.exitCode != 0 ? run.exitCode : 1;

        result.passed = false;
    }

    return finish();
}
} // namespace MakeASound::Pluginval
