// Apps/PluginHost without its window: its Host scans the fixture plugins through
// a worker into its own database, lists and filters them, loads one and runs a
// block through it.

#include <PluginHost.h>

#include <NanoTest/NanoTest.h>

#include <chrono>
#include <filesystem>
#include <thread>

using namespace nano;
using namespace MakeASound;

namespace
{
namespace fs = std::filesystem;

PluginHost::HostOptions fixtureOptions(const fs::path& folder)
{
    auto options = PluginHost::HostOptions();
    options.cachePath = (folder / "Plugins.json").string();
    options.folders = {MAKEASOUND_HOST_FIXTURE_VST3_DIR};
    options.useDefaultFolders = false;
    options.scanFormats = {PluginFormat::VST3};
    options.worker.executable = MAKEASOUND_FAKE_SCAN_WORKER;
    options.worker.argumentPrefix = {
        "--fake-worker", "formats", "--makeasound-scan"};
    options.scanTimeoutMs = 2000;
    options.startsAudio = false;
    return options;
}

int indexNamed(const PluginHost::Host& host, std::string_view name)
{
    for (auto i = 0; i < host.plugins().size(); ++i)
        if (host.plugins()[i].name == name)
            return i;

    return -1;
}

auto tHost = test("HostingPluginHost/theHostScansListsAndLoads") = []
{
    auto folder = fs::temp_directory_path() / "MakeASoundPluginHostTests" / "host";
    fs::remove_all(folder);

    auto host = PluginHost::Host(fixtureOptions(folder));
    auto changes = 0;
    host.onChanged = [&changes] { ++changes; };

    check(host.plugins().empty());

    host.scan(false, false);
    check(host.isScanning());

    auto start = std::chrono::steady_clock::now();

    while (host.isScanning()
           && std::chrono::steady_clock::now() - start < std::chrono::seconds(60))
    {
        host.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    check(!host.isScanning());
    check(changes > 0);
    check(host.plugins().size() == 2);
    check(host.status().find("2 crashed") != std::string::npos);

    host.setFilter("instrument");
    check(host.plugins().size() == 1);
    check(host.plugins()[0].name == "HostFixtureInstrument");

    host.setFilter({});
    auto effect = indexNamed(host, "HostFixtureEffect");
    check(effect >= 0);
    check(!host.load(-1));
    check(host.load(effect));
    check(host.plugin() != nullptr && host.plugin()->name() == "HostFixtureEffect");

    // No editor factory, no editor.
    check(host.createEditor() == nullptr);

    // A block through it, as the device would run one.
    auto layout = host.plugin()->getBusLayout();
    auto context = ProcessContext();
    context.prepare(layout);
    context.inputs[0].setSize(2, 64);
    context.outputs[0].setSize(2, 64);
    host.plugin()->prepare(
        {.sampleRate = 48000, .maxBlockSize = 64, .layout = layout});

    for (auto channel: context.inputs[0])
        for (auto& sample: channel)
            sample = 0.5f;

    host.plugin()->process(context);
    check(context.outputs[0][0][0] == 0.5f);
    host.idle();

    // A second host reads the same database without scanning.
    auto again = PluginHost::Host(fixtureOptions(folder));
    check(again.plugins().size() == 2);

    fs::remove_all(folder);
};
} // namespace
