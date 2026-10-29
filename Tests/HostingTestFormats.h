#pragma once

// Stand-in formats for the scan suites and FakeScanWorker: they claim targets and
// describe them without loading anything, so the pipeline around a format (the
// manager, the worker protocol, the coordinator, the sweep, the cache) is tested
// on its own.
//
// StubFileFormat claims *.vst3 paths as VST3 and describes one class per module,
// named after the bundle. StubRegistryFormat claims "cccc:cccc:cccc" identifiers
// as AU from a list the test fills in, and stamps each as its version, the way an
// Audio Unit stamps.

#include <MakeASound/Hosting/MakeASoundHosting.h>

#include <ScanProtocol/ScanWorker.h>

#include <filesystem>
#include <map>
#include <string>
#include <string_view>

namespace HostingTestFormats
{
using namespace MakeASound;
using namespace MakeASound::Hosting;
using namespace MakeASound::ScanProtocol;
using EA::makeOwned;

// The database's description, not the plugin core's, where a unity build sees
// both.
using MakeASound::Hosting::PluginDescription;

class StubFileFormat : public HostFormat
{
public:
    PluginFormat format() const override { return PluginFormat::VST3; }

    Vector<std::string> defaultSearchFolders() const override { return {}; }

    Vector<std::string>
        findTargets(const Vector<std::string>& folders) const override
    {
        return findModulesWithExtension(folders, ".vst3");
    }

    bool ownsTarget(std::string_view target) const override
    {
        return Text::endsWithIgnoringCase(target, ".vst3");
    }

    ModuleScan scan(std::string_view target) const override
    {
        auto scan = ModuleScan();
        scan.path = std::string(target);
        scan.setStamp(stamp(target));

        auto description = PluginDescription();
        description.path = scan.path;
        description.name =
            std::filesystem::path(std::string(target)).stem().string();
        description.classId = "STUBCLASS";
        description.vendor = "MakeASound";
        description.subCategories = "Fx";
        description.numAudioInputs = 2;
        description.numAudioOutputs = 2;
        scan.classes.add(description);

        // A module named Refuses.vst3 answers like a plugin that will not load.
        if (description.name == "Refuses")
        {
            scan.classes.clear();
            scan.error = "the stub refuses to load";
        }

        return scan;
    }
};

class StubRegistryFormat : public HostFormat
{
public:
    PluginFormat format() const override { return PluginFormat::AU; }

    Vector<std::string> defaultSearchFolders() const override { return {}; }

    Vector<std::string> findTargets(const Vector<std::string>&) const override
    {
        auto targets = Vector<std::string>();

        for (const auto& [identifier, version]: installed)
            targets.add(identifier);

        return targets;
    }

    bool ownsTarget(std::string_view target) const override
    {
        return target.size() == 14 && target[4] == ':' && target[9] == ':';
    }

    bool targetExists(std::string_view target) const override
    {
        return installed.count(std::string(target)) != 0;
    }

    ModuleStamp stamp(std::string_view target) const override
    {
        const auto found = installed.find(std::string(target));

        if (found == installed.end())
            return {};

        return {found->second, 0};
    }

    ModuleScan scan(std::string_view target) const override
    {
        auto scan = ModuleScan();
        scan.path = std::string(target);
        scan.setStamp(stamp(target));

        auto description = PluginDescription();
        description.format = PluginFormat::AU;
        description.path = scan.path;
        description.classId = scan.path;
        description.name = "Registry " + scan.path;
        scan.classes.add(description);

        return scan;
    }

    // identifier -> component version
    std::map<std::string, std::int64_t> installed;
};

// A manager holding the file format alone.
struct StubFormats : FormatManager
{
    StubFormats() { add(EA::makeOwned<StubFileFormat>()); }
};

} // namespace HostingTestFormats
