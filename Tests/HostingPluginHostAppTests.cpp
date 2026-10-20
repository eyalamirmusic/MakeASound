// The PluginHost app binary as its own scan worker, the way its scans relaunch
// it: no window, one result line.

#include <MakeASound/Hosting/MakeASoundHosting.h>

#include <eacp/Core/Process/Process.h>

#include <NanoTest/NanoTest.h>

#include <filesystem>

namespace HostingPluginHostAppTests
{
using namespace nano;
using namespace MakeASound;

namespace
{
auto tWorker = test("HostingPluginHost/theAppIsItsOwnScanWorker") = []
{
    auto fixture = std::filesystem::path(MAKEASOUND_HOST_FIXTURE_VST3_DIR)
                   / "HostFixtureEffect.vst3";

    auto options = eacp::Processes::ProcessOptions();
    options.executable = MAKEASOUND_PLUGIN_HOST;
    options.arguments = {"--makeasound-scan", fixture.string()};
    options.noWindow = true;

    auto result = eacp::Processes::run(std::move(options));
    check(result.exitCode == 0);

    auto scan = Hosting::decodeScanResult(result.output);
    check(scan.has_value() && scan->succeeded());
    check(scan.has_value() && scan->classes.size() == 1
          && scan->classes[0].name == "HostFixtureEffect");
};
} // namespace
} // namespace HostingPluginHostAppTests
