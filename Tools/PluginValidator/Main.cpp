// PluginValidator: pluginval, auval or aaxval over the bundles and folders named
// on the command line (--help lists the options).

#include <MakeASound/Plugin/Validation/Pluginval.h>

#include <eacp/Core/App/Args.h>
#include <eacp/Core/Utils/Console.h>
#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/StdPath.h>
#include <eacp/Core/Utils/Strings.h>

#include <algorithm>
#include <exception>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <cmath>

namespace Pluginval = MakeASound::Pluginval;
namespace Console = eacp::Console;
namespace Strings = eacp::Strings;

namespace
{
struct Arguments
{
    Pluginval::Options options;
    eacp::FilePath logs {std::filesystem::temp_directory_path() / "PluginValidator"};
    MakeASound::Vector<eacp::FilePath> bundles;
};

bool isFolderOfBundles(const eacp::FilePath& path)
{
    auto folder = eacp::toStdPath(path);
    auto error = std::error_code {};

    if (!std::filesystem::is_directory(folder, error))
        return false;

    if (!folder.has_filename())
        folder = folder.parent_path();

    auto extension = folder.extension();
    return extension != ".vst3" && extension != ".component"
           && extension != ".aaxplugin";
}

// Each folder named becomes the bundles directly inside it.
MakeASound::Vector<eacp::FilePath>
    expandFolders(const MakeASound::Vector<eacp::FilePath>& paths)
{
    auto bundles = MakeASound::Vector<eacp::FilePath> {};

    for (const auto& path: paths)
    {
        if (!isFolderOfBundles(path))
        {
            bundles.push_back(path);
            continue;
        }

        for (const auto& bundle: Pluginval::findBundles(path))
            bundles.push_back(bundle);
    }

    return bundles;
}

std::string toolFor(const eacp::FilePath& bundle)
{
    if (Pluginval::isAudioUnit(bundle))
        return "auval";

    return Pluginval::isAAX(bundle) ? "aaxval" : "pluginval";
}

std::string bundleName(const eacp::FilePath& bundle)
{
    auto path = eacp::toStdPath(bundle);

    if (!path.has_filename())
        path = path.parent_path();

    return eacp::FilePath {path.filename()}.str();
}

eacp::FilePath writeLog(const Pluginval::Result& result,
                        const eacp::FilePath& directory)
{
    eacp::Files::createDirectories(directory);

    auto name = bundleName(result.bundle);
    std::replace(name.begin(), name.end(), ' ', '-');

    auto path = directory / (name + ".log");
    auto file = std::ofstream {eacp::toStdPath(path), std::ios::binary};
    file << result.log;

    return path;
}

void printTail(const std::string& log, int count)
{
    auto lines = MakeASound::Vector<std::string> {};
    auto stream = std::istringstream {log};

    for (auto line = std::string {}; std::getline(stream, line);)
        if (line.find_first_not_of(" \t\r") != std::string::npos)
            lines.push_back(line);

    auto first = std::max(0, lines.size() - count);

    for (auto i = first; i < lines.size(); ++i)
        Console::print("    " + lines[i]);
}

int run(const Arguments& args)
{
    auto needsPluginval = std::any_of(args.bundles.begin(),
                                      args.bundles.end(),
                                      [](const auto& bundle)
                                      { return toolFor(bundle) == "pluginval"; });

    auto pluginval =
        needsPluginval ? Pluginval::fetch(args.options) : eacp::FilePath {};
    auto failures = 0;

    for (const auto& bundle: args.bundles)
    {
        auto name = bundleName(bundle);
        Console::print(Strings::concat(toolFor(bundle), " ", name, " ..."));

        auto result = Pluginval::validate(pluginval, bundle, args.options);
        auto log = writeLog(result, args.logs);
        auto seconds = std::lround(result.seconds);

        if (result.passed)
        {
            Console::print(Strings::concat("PASS ", name, " (", seconds, " s)"));
            continue;
        }

        ++failures;
        Console::print(Strings::concat(
            "FAIL ", name, " (exit ", result.exitCode, ", ", seconds, " s)"));
        printTail(result.log, 25);
        Console::print("    full log: " + log.str());
    }

    return failures;
}
} // namespace

int main(int argc, char* argv[])
{
    auto args = Arguments {};
    auto paths = MakeASound::Vector<eacp::FilePath> {};
    auto& options = args.options;

    auto cli = eacp::Args::Parser {
        "PluginValidator",
        "Validates each bundle named, and every .vst3, .component and .aaxplugin "
        "in each folder named: a .vst3 through Tracktion's pluginval, a "
        ".component through auval -strict after installing it into the user's "
        "Components folder, an .aaxplugin through every aaxval test in the AAX "
        "Validator folder (both macOS only)."};

    cli.option("--strictness", options.strictness, "pluginval's strictness level")
        .atLeast(1)
        .atMost(10);
    cli.flag("--skip-gui-tests", options.guiTests, "skip pluginval's editor tests")
        .negated();
    cli.option("--timeout-ms",
               options.timeout.count,
               "pluginval's timeout per test, in milliseconds")
        .atLeast(1);
    cli.option("--version", options.version, "the pluginval release to fetch");
    cli.option("--stress", options.stress, "auval's -stress iterations, 0 for none")
        .atLeast(0);
    cli.option("--aax-tools",
               options.aaxTools,
               "the unpacked AAX Validator folder, holding CommandLineTools/dsh")
        .fromEnv("MAKEASOUND_AAX_TOOLS");
    cli.option("--logs", args.logs, "where each bundle's full log is written");
    cli.positional(paths, "bundle", "a bundle, or a folder of bundles");
    cli.epilogue("Exits with the number of bundles that failed, or 2 when the "
                 "arguments are unusable. It knows nothing of this build: what to "
                 "validate is always an argument, such as <build>/VST3.");

    if (auto code = cli.exitCodeFor(cli.parse(argc, argv)))
        return *code;

    try
    {
        args.bundles = expandFolders(paths);

        if (args.bundles.empty())
            return cli.fail("nothing to validate");

        return run(args);
    }
    catch (const std::exception& e)
    {
        Console::printError(Strings::concat("PluginValidator: ", e.what()));
        return 1;
    }
}
