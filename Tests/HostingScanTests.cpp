// Scanning third-party plugins: the worker's own executable path.

#include <MakeASound/Hosting/MakeASoundHosting.h>

#include <NanoTest/NanoTest.h>

#include <filesystem>

using namespace nano;
using namespace MakeASound::Hosting;

namespace
{
namespace fs = std::filesystem;

auto tExecutable = test("HostingScan/theExecutablePathIsThisBinary") = []
{
    const auto path = fs::path(currentExecutablePath());

    check(path.is_absolute());
    check(fs::exists(path));
    check(path.stem() == "MakeASoundTests");
};

auto tPlatformPath = test("HostingScan/thePlatformKnowsTheExecutable") = []
{
    const auto fromPlatform = fs::path(executablePathFromPlatform());

    check(fs::equivalent(fromPlatform, fs::path(currentExecutablePath())));
};
} // namespace
