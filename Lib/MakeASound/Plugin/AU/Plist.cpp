#include "Plist.h"
#include "ComponentType.h"

#include <fstream>
#include <string>

namespace MakeASound::AU
{

namespace
{
enum class Form
{
    Component,
    AppExtension
};

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

std::string indent(int depth)
{
    return std::string(static_cast<size_t>(depth) * 4, ' ');
}

void writeKey(std::ofstream& out,
              std::string_view key,
              std::string_view value,
              int depth = 1)
{
    out << indent(depth) << "<key>" << key << "</key><string>" << escaped(value)
        << "</string>\n";
}

void writeHeader(std::ofstream& out)
{
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        << "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
           "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        << "<plist version=\"1.0\">\n"
        << "<dict>\n";
}

void writeFooter(std::ofstream& out)
{
    out << "</dict>\n"
        << "</plist>\n";
}

// sandboxSafe, or for a module that hosts plugins the resources they need.
void writeSandboxKeys(std::ofstream& out, bool hostsPlugins, int depth)
{
    if (!hostsPlugins)
    {
        out << indent(depth) << "<key>sandboxSafe</key><true/>\n";
        return;
    }

    auto inner = depth + 1;

    out << indent(depth) << "<key>resourceUsage</key>\n"
        << indent(depth) << "<dict>\n"
        << indent(inner) << "<key>network.client</key><true/>\n"
        << indent(inner)
        << "<key>temporary-exception.files.all.read-write</key><true/>\n"
        << indent(depth) << "</dict>\n";
}

void writeComponent(std::ofstream& out,
                    const ModuleDescription& module,
                    const PluginDescription& plugin,
                    Form form,
                    bool hostsPlugins,
                    int depth)
{
    auto info = componentInfoFor(module, plugin);
    auto name = std::string(module.vendor) + ": " + std::string(plugin.name);
    auto inner = depth + 1;

    out << indent(depth) << "<dict>\n";
    writeKey(out, "type", fourCCString(info.type), inner);
    writeKey(out, "subtype", fourCCString(info.subtype), inner);
    writeKey(out, "manufacturer", fourCCString(info.manufacturer), inner);
    writeKey(out, "name", name, inner);
    writeKey(out, "description", plugin.name, inner);
    out << indent(inner) << "<key>version</key><integer>"
        << versionNumber(plugin.version) << "</integer>\n";

    // An app extension is instantiated through its principal class instead.
    if (form == Form::Component)
        writeKey(out, "factoryFunction", "MakeASoundAUFactory", inner);

    writeSandboxKeys(out, hostsPlugins, inner);

    if (form == Form::AppExtension)
        out << indent(inner) << "<key>tags</key><array><string>"
            << escaped(componentTag(plugin.category)) << "</string></array>\n";

    out << indent(depth) << "</dict>\n";
}

void writeComponents(std::ofstream& out,
                     const ModuleDescription& module,
                     Form form,
                     bool hostsPlugins,
                     int depth)
{
    out << indent(depth) << "<key>AudioComponents</key>\n"
        << indent(depth) << "<array>\n";

    for (const auto& plugin: module.plugins)
        writeComponent(out, module, plugin, form, hostsPlugins, depth + 1);

    out << indent(depth) << "</array>\n";
}
} // namespace

bool writeAudioComponentsPlist(const ModuleDescription& module,
                               std::string_view bundleName,
                               std::string_view bundleId,
                               std::string_view executable,
                               const std::filesystem::path& output,
                               bool hostsPlugins)
{
    auto out = std::ofstream(output);

    if (!out)
        return false;

    writeHeader(out);
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

    writeComponents(out, module, Form::Component, hostsPlugins, 1);
    writeFooter(out);

    return out.good();
}

bool writeAppExtensionPlist(const ModuleDescription& module,
                            const AppExtensionPlist& bundle,
                            const std::filesystem::path& output)
{
    auto out = std::ofstream(output);

    if (!out)
        return false;

    writeHeader(out);
    writeKey(out, "CFBundleDevelopmentRegion", "English");
    writeKey(out, "CFBundleDisplayName", bundle.bundleName);
    writeKey(out, "CFBundleExecutable", bundle.executable);
    writeKey(out, "CFBundleIdentifier", bundle.bundleId);
    writeKey(out, "CFBundleInfoDictionaryVersion", "6.0");
    writeKey(out, "CFBundleName", bundle.bundleName);
    writeKey(out, "CFBundlePackageType", "XPC!");
    writeKey(out, "CFBundleVersion", module.version);
    writeKey(out, "CFBundleShortVersionString", module.version);

    out << "    <key>NSExtension</key>\n"
        << "    <dict>\n"
        << "        <key>NSExtensionAttributes</key>\n"
        << "        <dict>\n";

    // An app extension's sandbox is its entitlements' to widen, not resourceUsage.
    writeComponents(out, module, Form::AppExtension, false, 3);

    out << "        </dict>\n";

    writeKey(out,
             "NSExtensionPointIdentifier",
             bundle.hasView ? "com.apple.AudioUnit-UI" : "com.apple.AudioUnit",
             2);
    writeKey(out, "NSExtensionPrincipalClass", bundle.principalClass, 2);

    out << "    </dict>\n";
    writeFooter(out);

    return out.good();
}

} // namespace MakeASound::AU
