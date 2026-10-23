#pragma once

#include "../Core/Description.h"

#include <filesystem>
#include <string_view>

namespace MakeASound::AU
{

// The .component's Info.plist: the bundle keys, CFBundleVersion from the module's
// version, and one AudioComponents entry per plugin. False when it cannot write.
//
// Each entry declares `sandboxSafe`, unless hostsPlugins says the module loads
// third-party plugins inside itself: then each declares `resourceUsage` instead,
// network client plus read-write access to any file (Tamber Plug's keys). In a
// sandboxed AU host such as Logic's AUHostingService, a plugin hosted inside
// this one inherits its sandbox and would otherwise be denied its own content
// and licence files.
bool writeAudioComponentsPlist(const ModuleDescription& module,
                               std::string_view bundleName,
                               std::string_view bundleId,
                               std::string_view executable,
                               const std::filesystem::path& output,
                               bool hostsPlugins = false);

// What an AUv3 app extension's plist names beyond describeModule().
struct AppExtensionPlist
{
    std::string_view bundleName;
    std::string_view bundleId;
    std::string_view executable;

    // The NSExtensionPrincipalClass, the module's view controller factory.
    std::string_view principalClass;

    // com.apple.AudioUnit-UI with a view, com.apple.AudioUnit without.
    bool hasView = true;
};

// The .appex's Info.plist: the bundle keys (package type XPC!) and NSExtension,
// whose attributes carry one AudioComponents entry per plugin, as the .component's
// less factoryFunction, plus tags. The platform keys (MinimumOSVersion,
// CFBundleSupportedPlatforms, UIDeviceFamily) are the packaging's to add.
bool writeAppExtensionPlist(const ModuleDescription& module,
                            const AppExtensionPlist& bundle,
                            const std::filesystem::path& output);

} // namespace MakeASound::AU
