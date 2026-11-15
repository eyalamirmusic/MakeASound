// A module's stamp on Linux is Unix seconds, as Tamber Plug's macOS databases
// have it, not libstdc++'s file clock count from 2174.

#include <MakeASound/Hosting/MakeASoundHosting.h>

#include <NanoTest/NanoTest.h>

#include <chrono>
#include <filesystem>
#include <fstream>

namespace HostingStampTests
{
using namespace nano;
using namespace MakeASound::Hosting;

namespace
{
namespace fs = std::filesystem;

// What HostingFixtures.h's Plug database has for its modules.
constexpr auto plugSeconds = std::int64_t {1791931959};

auto tUnix = test("HostingCache/aStampIsUnixSecondsOnLinux") = []
{
    const auto root = fs::temp_directory_path() / "MakeASoundHostingStampTests";
    fs::remove_all(root);

    const auto binary =
        root / "Stamped.vst3" / "Contents" / "aarch64-linux" / "Stamped.so";
    fs::create_directories(binary.parent_path());
    std::ofstream(binary, std::ios::binary) << "module";

    const auto when = std::chrono::sys_seconds {std::chrono::seconds {plugSeconds}};
    fs::last_write_time(binary, std::chrono::file_clock::from_sys(when));

    check(moduleStamp(binary.string()).lastModified == plugSeconds);
    check(moduleStamp((root / "Stamped.vst3").string()).lastModified == plugSeconds);

    fs::remove_all(root);
};
} // namespace
} // namespace HostingStampTests
