#include "Plist.h"
#include "ComponentType.h"

#include <fstream>
#include <string>

namespace MakeASound::AU
{

namespace
{
std::string escaped(std::string_view text)
{
    auto result = std::string {};

    for (auto c: text)
    {
        switch (c)
        {
            case '&':
                result += "&amp;";
                break;
            case '<':
                result += "&lt;";
                break;
            case '>':
                result += "&gt;";
                break;
            default:
                result += c;
        }
    }

    return result;
}

void writeKey(std::ofstream& out, std::string_view key, std::string_view value)
{
    out << "    <key>" << key << "</key><string>" << escaped(value) << "</string>\n";
}

void writeComponent(std::ofstream& out,
                    const ModuleDescription& module,
                    const PluginDescription& plugin)
{
    auto info = componentInfoFor(module, plugin);
    auto name = std::string(module.vendor) + ": " + std::string(plugin.name);

    out << "        <dict>\n"
        << "            <key>type</key><string>" << escaped(fourCCString(info.type))
        << "</string>\n"
        << "            <key>subtype</key><string>"
        << escaped(fourCCString(info.subtype)) << "</string>\n"
        << "            <key>manufacturer</key><string>"
        << escaped(fourCCString(info.manufacturer)) << "</string>\n"
        << "            <key>name</key><string>" << escaped(name) << "</string>\n"
        << "            <key>description</key><string>" << escaped(plugin.name)
        << "</string>\n"
        << "            <key>version</key><integer>"
        << versionNumber(plugin.version) << "</integer>\n"
        << "            <key>factoryFunction</key><string>MakeASoundAUFactory"
           "</string>\n"
        << "            <key>sandboxSafe</key><true/>\n"
        << "        </dict>\n";
}
} // namespace

bool writeAudioComponentsPlist(const ModuleDescription& module,
                               std::string_view bundleName,
                               std::string_view bundleId,
                               std::string_view executable,
                               const std::filesystem::path& output)
{
    auto out = std::ofstream(output);

    if (!out)
        return false;

    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        << "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
           "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        << "<plist version=\"1.0\">\n"
        << "<dict>\n";

    writeKey(out, "CFBundleDevelopmentRegion", "English");
    writeKey(out, "CFBundleExecutable", executable);
    writeKey(out, "CFBundleIdentifier", bundleId);
    writeKey(out, "CFBundleInfoDictionaryVersion", "6.0");
    writeKey(out, "CFBundleName", bundleName);
    writeKey(out, "CFBundlePackageType", "BNDL");
    writeKey(out, "CFBundleSignature", "????");

    // macOS caches AudioComponents keyed by this, so it must move with them.
    writeKey(out, "CFBundleVersion", module.version);
    writeKey(out, "CFBundleShortVersionString", module.version);

    out << "    <key>AudioComponents</key>\n"
        << "    <array>\n";

    for (const auto& plugin: module.plugins)
        writeComponent(out, module, plugin);

    out << "    </array>\n"
        << "</dict>\n"
        << "</plist>\n";

    return out.good();
}

} // namespace MakeASound::AU
