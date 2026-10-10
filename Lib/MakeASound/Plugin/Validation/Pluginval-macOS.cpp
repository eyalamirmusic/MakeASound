#include "PluginvalPlatform.h"

#include <eacp/Core/Utils/StdPath.h>

namespace MakeASound::Pluginval
{
Asset platformAsset()
{
    return {"pluginval_macOS.zip", "pluginval.app/Contents/MacOS/pluginval"};
}

void makeExecutable(const eacp::FilePath& binary)
{
    namespace fs = std::filesystem;
    auto executable =
        fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec;
    fs::permissions(eacp::toStdPath(binary), executable, fs::perm_options::add);
}
} // namespace MakeASound::Pluginval
