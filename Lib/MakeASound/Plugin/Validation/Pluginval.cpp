#include "Pluginval.h"
#include "AUValidator.h"
#include "PluginvalPlatform.h"

#include <eacp/Core/Process/Process.h>
#include <eacp/Core/Utils/StdPath.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <stdexcept>

namespace MakeASound::Pluginval
{
eacp::FilePath fetch(const Options& options)
{
    auto asset = platformAsset();
    auto url = "https://github.com/Tracktion/pluginval/releases/download/"
               + options.version + "/" + asset.archive;

    // Trusted once on disk: a release never changes, and a new one is a new
    // version, which the sidecar compares and downloads for.
    auto resource = eacp::OnlineResource::fetch(
        {.info = {.name = "pluginval " + options.version,
                  .url = url,
                  .fileName = asset.archive,
                  .version = options.version},
         .directory = options.directory,
         .freshness = eacp::OnlineResource::Freshness::trust});

    auto binary = *resource / asset.binary;

    if (!std::filesystem::is_regular_file(eacp::toStdPath(binary)))
        throw std::runtime_error("pluginval: " + binary.str() + " is not in "
                                 + asset.archive);

    makeExecutable(binary);
    return binary;
}

static std::filesystem::path bundlePath(const eacp::FilePath& bundle)
{
    auto path = eacp::toStdPath(bundle);
    return path.has_filename() ? path : path.parent_path();
}

bool isAudioUnit(const eacp::FilePath& bundle)
{
    return bundlePath(bundle).extension() == ".component";
}

Result validate(const eacp::FilePath& pluginval,
                const eacp::FilePath& bundle,
                const Options& options)
{
    if (isAudioUnit(bundle))
        return validateAudioUnit(bundle, options);

    auto arguments = Vector<std::string> {"--strictness-level",
                                          std::to_string(options.strictness),
                                          "--timeout-ms",
                                          std::to_string(options.timeout.count),
                                          "--verbose"};

    if (!options.guiTests)
        arguments.emplace_back("--skip-gui-tests");

    arguments.emplace_back("--validate");
    arguments.emplace_back(bundle.str());

    auto started = std::chrono::steady_clock::now();
    auto run = eacp::Processes::run(pluginval.str(), arguments);
    auto elapsed = std::chrono::steady_clock::now() - started;

    auto result = Result {.bundle = bundle};
    result.seconds = std::chrono::duration<double>(elapsed).count();
    result.log = run.output + run.errorOutput;

    if (!run.launched)
        result.log += "could not launch " + pluginval.str() + "\n";

    if (run.launched && run.exited)
        result.exitCode = run.exitCode;

    result.passed = result.exitCode == 0;
    return result;
}

Vector<eacp::FilePath> findBundles(const eacp::FilePath& directory)
{
    namespace fs = std::filesystem;

    auto bundles = Vector<eacp::FilePath> {};
    auto error = std::error_code {};

    for (const auto& entry:
         fs::directory_iterator(eacp::toStdPath(directory), error))
        if (auto extension = entry.path().extension();
            extension == ".vst3" || extension == ".component")
            bundles.emplace_back(entry.path());

    std::sort(bundles.begin(),
              bundles.end(),
              [](const auto& a, const auto& b) { return a.str() < b.str(); });

    return bundles;
}
} // namespace MakeASound::Pluginval
