#pragma once

#include "../Core/Description.h"

#include <filesystem>
#include <string_view>

namespace MakeASound::AU
{

// The .component's Info.plist: the bundle keys, CFBundleVersion from the module's
// version, and one AudioComponents entry per plugin. False when it cannot write.
bool writeAudioComponentsPlist(const ModuleDescription& module,
                               std::string_view bundleName,
                               std::string_view bundleId,
                               std::string_view executable,
                               const std::filesystem::path& output);

} // namespace MakeASound::AU
