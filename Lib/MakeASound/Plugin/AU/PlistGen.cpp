// <Name>-AUPlistGen: writes the .component's Info.plist from describeModule(),
// one AudioComponents entry per plugin, run after every link of <Name>-AU.
//   <Name>-AUPlistGen <bundle name> <bundle id> <executable> <output path>
#include "ComponentType.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <string_view>

using namespace MakeASound;

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
    auto info = AU::componentInfoFor(module, plugin);
    auto name = std::string(module.vendor) + ": " + std::string(plugin.name);

    out << "        <dict>\n"
        << "            <key>type</key><string>"
        << escaped(AU::fourCCString(info.type)) << "</string>\n"
        << "            <key>subtype</key><string>"
        << escaped(AU::fourCCString(info.subtype)) << "</string>\n"
        << "            <key>manufacturer</key><string>"
        << escaped(AU::fourCCString(info.manufacturer)) << "</string>\n"
        << "            <key>name</key><string>" << escaped(name) << "</string>\n"
        << "            <key>description</key><string>" << escaped(plugin.name)
        << "</string>\n"
        << "            <key>version</key><integer>"
        << AU::versionNumber(plugin.version) << "</integer>\n"
        << "            <key>factoryFunction</key><string>MakeASoundAUFactory"
           "</string>\n"
        << "            <key>sandboxSafe</key><true/>\n"
        << "        </dict>\n";
}
} // namespace

int main(int argc, char** argv)
{
    if (argc != 5)
    {
        std::fprintf(stderr,
                     "usage: %s <bundle name> <bundle id> <executable> <output>\n",
                     argv[0]);
        return 1;
    }

    try
    {
        auto module = describeModule();
        auto out = std::ofstream(argv[4]);

        if (!out)
        {
            std::fprintf(stderr, "cannot write %s\n", argv[4]);
            return 1;
        }

        out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            << "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
               "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
            << "<plist version=\"1.0\">\n"
            << "<dict>\n";

        writeKey(out, "CFBundleDevelopmentRegion", "English");
        writeKey(out, "CFBundleExecutable", argv[3]);
        writeKey(out, "CFBundleIdentifier", argv[2]);
        writeKey(out, "CFBundleInfoDictionaryVersion", "6.0");
        writeKey(out, "CFBundleName", argv[1]);
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

        return out.good() ? 0 : 1;
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
}
