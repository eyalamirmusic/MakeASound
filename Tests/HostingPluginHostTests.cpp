// Apps/PluginHost without its window: its Host scans the fixture plugins through
// a worker into its own database, lists and filters them, loads one and runs a
// block through it.

#include <PluginHost.h>

#include <ScanProtocol/ScanWorker.h>

#include <NanoTest/NanoTest.h>

#include <chrono>
#include <filesystem>
#include <thread>

namespace HostingPluginHostTests
{
using namespace nano;
using namespace MakeASound;

namespace
{
namespace fs = std::filesystem;

PluginHost::HostOptions fixtureOptions(const fs::path& folder)
{
    auto options = PluginHost::HostOptions();
    options.scan.cachePath = (folder / "Plugins.json").string();
    options.scan.searchFolders = {MAKEASOUND_HOST_FIXTURE_VST3_DIR};
    options.scan.useDefaultFolders = false;
    options.scan.formats = {PluginFormat::VST3};
    options.scan.worker.executable = MAKEASOUND_FAKE_SCAN_WORKER;
    options.scan.worker.argumentPrefix = {
        "--fake-worker", "formats", "--makeasound-scan"};
    options.scan.worker.decode = ScanProtocol::decodeScanResult;
    options.scan.timeoutMs = 2000;
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

// The device and MIDI picks.

DeviceInfo deviceNamed(const std::string& name, int id, int outputs, int inputs)
{
    auto device = DeviceInfo {};
    device.id = id;
    device.name = name;
    device.outputChannels = outputs;
    device.inputChannels = inputs;
    device.sampleRates = {44100, 48000};
    device.preferredSampleRate = 48000;
    return device;
}

StreamConfig outputOn(const DeviceInfo& device, int rate, int block)
{
    auto config = StreamConfig {};
    config.output = StreamParameters(device, false);
    config.sampleRate = rate;
    config.maxBlockSize = block;
    return config;
}

auto tConfigNoPick = test("HostingPluginHost/noAudioPickIsTheDefault") = []
{
    auto fallback = outputOn(deviceNamed("Built-in", 1, 2, 0), 44100, 512);
    auto devices = Vector<DeviceInfo> {deviceNamed("Interface", 2, 4, 2)};

    auto chosen = PluginHost::chooseConfig(std::nullopt, devices, fallback);

    check(chosen.output->device.name == "Built-in");
    check(chosen.maxBlockSize == 512);
};

auto tConfigPick = test("HostingPluginHost/anAudioPickIsRepointedByName") = []
{
    auto fallback = outputOn(deviceNamed("Built-in", 1, 2, 0), 44100, 512);
    auto devices = Vector<DeviceInfo> {deviceNamed("Built-in", 1, 2, 0),
                                       deviceNamed("Interface", 7, 4, 2)};

    // Saved last launch, when the interface had another id.
    auto picked = outputOn(deviceNamed("Interface", 3, 4, 2), 48000, 128);
    picked.input = StreamParameters(deviceNamed("Interface", 3, 4, 2), true);

    auto chosen = PluginHost::chooseConfig(picked, devices, fallback);

    check(chosen.output->device.id == 7);
    check(chosen.input.has_value() && chosen.input->device.id == 7);
    check(chosen.sampleRate == 48000);
    check(chosen.maxBlockSize == 128);
};

auto tConfigGone = test("HostingPluginHost/aPickedDeviceGoneFallsBack") = []
{
    auto fallback = outputOn(deviceNamed("Built-in", 1, 2, 0), 44100, 512);
    auto devices = Vector<DeviceInfo> {deviceNamed("Built-in", 1, 2, 0)};
    auto picked = outputOn(deviceNamed("Unplugged", 3, 2, 0), 48000, 128);

    auto chosen = PluginHost::chooseConfig(picked, devices, fallback);

    check(chosen.output->device.name == "Built-in");
    check(chosen.maxBlockSize == 128);
};

auto tMidiPick = test("HostingPluginHost/midiInputsArePickedByName") = []
{
    auto ports = Vector<MidiPortInfo> {{.id = 4, .name = "Keys"},
                                       {.id = 5, .name = "Pads"},
                                       {.id = 6, .name = "Keys"}};

    // Nothing picked: every input, as the host always opened.
    auto every = PluginHost::chooseMidiInputs(std::nullopt, ports);
    check(every.size() == 3);

    // Picked: those names, two of one model each taking its own port, and a
    // controller not plugged in today skipped.
    auto picked = PluginHost::chooseMidiInputs(
        Vector<std::string> {"Keys", "Unplugged", "Keys"}, ports);
    check(picked.size() == 2);
    check(picked[0] == 4 && picked[1] == 6);

    // Picked none: none.
    check(PluginHost::chooseMidiInputs(Vector<std::string> {}, ports).empty());
};

auto tPersist = test("HostingPluginHost/thePicksSurviveARelaunch") = []
{
    auto folder = fs::temp_directory_path() / "MakeASoundPluginHostTests" / "picks";
    fs::remove_all(folder);

    auto file = (folder / "settings.json").string();

    // A settings file from an earlier launch, naming hardware not here now.
    auto earlier = Standalone::Settings {};
    earlier.audio = outputOn(deviceNamed("Unplugged", 3, 2, 0), 48000, 128);
    earlier.midiInputPorts = {"Unplugged Keys"};
    check(Standalone::saveSettings(file, earlier));

    auto options = fixtureOptions(folder);
    options.settingsFile = file;

    auto fallback = DeviceManager().getDefaultOutputConfig();

    {
        auto host = PluginHost::Host(options);

        // The output device is gone, so the default's side; the block is kept.
        auto config = host.config();
        check(config.output.has_value() == fallback.output.has_value());

        if (config.output && fallback.output)
            check(config.output->device.name == fallback.output->device.name);

        check(config.maxBlockSize == 128);

        // A new pick, with no plugin loaded: saved, nothing started.
        host.applyConfig(
            outputOn(deviceNamed("Also Unplugged", 8, 2, 0), 44100, 64));
        check(!host.isAudioRunning());
    }

    auto saved = Standalone::loadSettings(file);
    check(saved.has_value());
    check(saved->audio.output->device.name == "Also Unplugged");
    check(saved->audio.maxBlockSize == 64);

    // MIDI was never touched, so the pick stays, absent port included.
    check(saved->midiInputPorts.size() == 1);
    check(saved->midiInputPorts[0] == "Unplugged Keys");

    {
        auto host = PluginHost::Host(options);
        check(host.config().maxBlockSize == 64);

        host.resetAudioAndMidi();
    }

    // A reset forgets the audio pick and opens no input next time.
    auto reset = Standalone::loadSettings(file);
    check(reset.has_value());
    check(!reset->audio.output && !reset->audio.input);
    check(reset->midiInputPorts.empty());

    auto fresh = PluginHost::Host(options);
    check(fresh.config().maxBlockSize == fallback.maxBlockSize);

    fs::remove_all(folder);
};
} // namespace
} // namespace HostingPluginHostTests
