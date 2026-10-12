#include "PluginvalPlatform.h"

#include <eacp/Core/Utils/WinInclude.h>

#include <stdexcept>

namespace MakeASound::Pluginval
{
// The machine, not this process: an x64 pluginval emulated on ARM64 could not load
// an ARM64 bundle anyway.
static bool isArm64Machine()
{
    auto process = USHORT {};
    auto native = USHORT {};

    if (!IsWow64Process2(GetCurrentProcess(), &process, &native))
        return false;

    return native == IMAGE_FILE_MACHINE_ARM64;
}

Asset platformAsset()
{
    if (isArm64Machine())
        throw std::runtime_error("Tracktion publishes no Windows ARM64 pluginval");

    return {"pluginval_Windows.zip", "pluginval.exe"};
}

void makeExecutable(const eacp::FilePath&)
{
}
} // namespace MakeASound::Pluginval
