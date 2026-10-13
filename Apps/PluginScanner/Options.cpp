#include "Options.h"

#include <charconv>
#include <optional>
#include <string_view>

namespace PluginScanner
{

namespace
{
// The argument after `index`, advancing past it; nullopt when there is none.
std::optional<std::string> valueAfter(int argc, char** argv, int& index)
{
    if (index + 1 >= argc || argv[index + 1] == nullptr)
        return std::nullopt;

    ++index;
    return std::string(argv[index]);
}

std::optional<int> toInt(const std::string& text)
{
    auto value = 0;
    const auto* last = text.data() + text.size();
    const auto result = std::from_chars(text.data(), last, value);

    if (result.ec != std::errc() || result.ptr != last)
        return std::nullopt;

    return value;
}

std::optional<PluginFormat> toFormat(std::string_view text)
{
    if (text == "vst3" || text == "VST3")
        return PluginFormat::VST3;

    if (text == "au" || text == "AU")
        return PluginFormat::AU;

    return std::nullopt;
}
} // namespace

Options parseOptions(int argc, char** argv)
{
    auto options = Options();

    const auto missing = [&options](std::string_view flag)
    { options.error = std::string(flag) + " needs a value"; };

    for (auto i = 1; i < argc && options.error.empty(); ++i)
    {
        const auto argument = std::string_view(argv[i] != nullptr ? argv[i] : "");

        if (argument == "--help" || argument == "-h")
        {
            options.mode = Mode::help;
            return options;
        }

        if (argument == "--output")
        {
            if (auto value = valueAfter(argc, argv, i))
                options.outputPath = std::move(*value);
            else
                missing(argument);
        }
        else if (argument == "--folder")
        {
            if (auto value = valueAfter(argc, argv, i))
                options.folders.addIfNotThere(*value);
            else
                missing(argument);
        }
        else if (argument == "--no-default-folders")
        {
            options.useDefaultFolders = false;
        }
        else if (argument == "--format")
        {
            auto value = valueAfter(argc, argv, i);

            if (!value.has_value())
                missing(argument);
            else if (const auto format = toFormat(*value))
                options.formats.addIfNotThere(*format);
            else
                options.error = "--format is vst3 or au, not '" + *value + "'";
        }
        else if (argument == "--force")
        {
            options.force = true;
        }
        else if (argument == "--jobs")
        {
            auto value = valueAfter(argc, argv, i);

            if (!value.has_value())
                missing(argument);
            else if (const auto jobs = toInt(*value); jobs && *jobs > 0)
                options.jobs = *jobs;
            else
                options.error =
                    "--jobs needs a positive number, not '" + *value + "'";
        }
        else if (argument == "--timeout")
        {
            auto value = valueAfter(argc, argv, i);

            if (!value.has_value())
                missing(argument);
            else if (const auto timeout = toInt(*value))
                options.timeoutMs = *timeout;
            else
                options.error = "--timeout needs milliseconds, not '" + *value + "'";
        }
        else
        {
            options.error =
                "unknown argument '" + std::string(argument) + "' (try --help)";
        }
    }

    return options;
}

std::string helpText()
{
    return R"(PluginScanner: describes the audio plugins installed on this machine.

Usage:
  PluginScanner [options]                       sweep and write the database
  PluginScanner --makeasound-scan <target>      scan one target (worker)

A sweep finds every VST3 module in the platform's VST3 folders and, on macOS,
every hostable Audio Unit, then loads each one in a WORKER PROCESS: this binary
relaunched with --makeasound-scan, several at a time. A plugin that crashes or
hangs takes down only its own worker and is recorded as crashed.

A sweep is incremental: only plugins that are new or changed since the last one
are loaded. A plugin that failed stays blacklisted until it changes, and one no
longer installed is dropped. --force starts from nothing.

  --output <file>        the database to write. Default: Plugins.json in this
                         app's own application-data folder.
  --folder <dir>         another place to look for VST3 modules; repeatable.
  --no-default-folders   look only in the --folder paths.
  --format <vst3|au>     sweep only this format; repeatable. Cached entries of
                         other formats are left alone.
  --force                delete the database first and scan everything.
  --jobs <n>             workers at once. Default: the core count, 1..8.
  --timeout <ms>         how long one plugin may take before its worker is
                         killed. Default 30000; 0 disables the watchdog.
  --help                 this text.

Worker mode loads one target, a .vst3 module or an Audio Unit identifier
(type:subtype:manufacturer), and prints one line to stdout: MAKEASOUND_SCAN:
followed by the result as compact JSON. See MakeASound's Docs/PluginScanning.md.

Exit codes:
  0  answered (a plugin that failed to load is an answer, not a failure)
  2  the arguments made no sense
  3  worker: nothing is installed under that name
  4  worker: no format claims the target
  5  the database could not be written
)";
}

} // namespace PluginScanner
