// PluginValidator [--strictness N] [--skip-gui-tests] [--timeout-ms N]
//                 [--version vX.Y.Z] [--stress N] [--logs <dir>]
//                 <bundle or folder>...
//
// Validates each bundle named, and every .vst3 and .component in each folder named:
// a .vst3 through Tracktion's pluginval, a .component through auval -strict after
// installing it into the user's Components folder (macOS only). Exits with the
// number that failed. It knows nothing of this build: what to validate is always
// an argument.

#include <MakeASound/Plugin/Validation/Pluginval.h>

#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/StdPath.h>

#include <algorithm>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

namespace Pluginval = MakeASound::Pluginval;

namespace
{
struct Arguments
{
    Pluginval::Options options;
    eacp::FilePath logs {std::filesystem::temp_directory_path() / "PluginValidator"};
    MakeASound::Vector<eacp::FilePath> bundles;
};

void printUsage()
{
    std::cerr << "usage: PluginValidator [--strictness N] [--skip-gui-tests] "
                 "[--timeout-ms N] [--version vX.Y.Z] [--stress N] [--logs <dir>] "
                 "<bundle or folder>...\n";
}

bool isFolderOfBundles(const eacp::FilePath& path)
{
    auto folder = eacp::toStdPath(path);
    auto error = std::error_code {};

    if (!std::filesystem::is_directory(folder, error))
        return false;

    if (!folder.has_filename())
        folder = folder.parent_path();

    auto extension = folder.extension();
    return extension != ".vst3" && extension != ".component";
}

Arguments parseArguments(int argc, char* argv[])
{
    auto args = Arguments {};

    for (auto i = 1; i < argc; ++i)
    {
        auto arg = std::string_view {argv[i]};

        auto value = [&]
        {
            if (i + 1 >= argc)
                throw std::invalid_argument(std::string(arg) + " needs a value");

            return std::string {argv[++i]};
        };

        if (arg == "--strictness")
            args.options.strictness = std::stoi(value());
        else if (arg == "--skip-gui-tests")
            args.options.guiTests = false;
        else if (arg == "--timeout-ms")
            args.options.timeout = {std::stoll(value())};
        else if (arg == "--version")
            args.options.version = value();
        else if (arg == "--stress")
            args.options.stress = std::stoi(value());
        else if (arg == "--logs")
            args.logs = eacp::FilePath {value()};
        else if (arg.starts_with("--"))
            throw std::invalid_argument("unknown option " + std::string(arg));
        else if (auto path = eacp::FilePath {std::string {arg}};
                 isFolderOfBundles(path))
            for (auto& bundle: Pluginval::findBundles(path))
                args.bundles.push_back(std::move(bundle));
        else
            args.bundles.emplace_back(std::move(path));
    }

    if (args.bundles.empty())
        throw std::invalid_argument("nothing to validate");

    return args;
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
        std::cout << "    " << lines[i] << '\n';
}

int run(const Arguments& args)
{
    auto needsPluginval = std::any_of(args.bundles.begin(),
                                      args.bundles.end(),
                                      [](const auto& bundle)
                                      { return !Pluginval::isAudioUnit(bundle); });

    auto pluginval =
        needsPluginval ? Pluginval::fetch(args.options) : eacp::FilePath {};
    auto failures = 0;

    for (const auto& bundle: args.bundles)
    {
        auto tool = Pluginval::isAudioUnit(bundle) ? "auval " : "pluginval ";
        std::cout << tool << bundleName(bundle) << " ..." << std::endl;

        auto result = Pluginval::validate(pluginval, bundle, args.options);
        auto log = writeLog(result, args.logs);
        auto seconds = static_cast<int>(result.seconds + 0.5);

        if (result.passed)
        {
            std::cout << "PASS " << bundleName(bundle) << " (" << seconds << " s)\n";
            continue;
        }

        ++failures;
        std::cout << "FAIL " << bundleName(bundle) << " (exit " << result.exitCode
                  << ", " << seconds << " s)\n";
        printTail(result.log, 25);
        std::cout << "    full log: " << log.str() << '\n';
    }

    return failures;
}
} // namespace

int main(int argc, char* argv[])
{
    try
    {
        return run(parseArguments(argc, argv));
    }
    catch (const std::invalid_argument& e)
    {
        std::cerr << "PluginValidator: " << e.what() << '\n';
        printUsage();
        return 1;
    }
    catch (const std::exception& e)
    {
        std::cerr << "PluginValidator: " << e.what() << '\n';
        return 1;
    }
}
