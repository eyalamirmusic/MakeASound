#include "ScanWorker.h"
#include "ExecutablePath.h"
#include "WorkerProcess.h"

#include <Miro/Reflection/Serialize.h>

#include <eacp/Core/App/AppEnvironment.h>

#include <cstdio>

namespace MakeASound::Hosting
{

namespace
{
ScanWorkerExit exitFor(TargetRequest request)
{
    switch (request)
    {
        case TargetRequest::noSuchTarget:
            return ScanWorkerExit::noSuchTarget;

        case TargetRequest::unknownKind:
            return ScanWorkerExit::unknownTarget;

        case TargetRequest::ok:
            break;
    }

    return ScanWorkerExit::ok;
}
} // namespace

WorkerCommand workerCommandFor(std::string executable)
{
    auto command = WorkerCommand();

    command.executable = std::move(executable);
    command.argumentPrefix.add(std::string(scanArgument));

    return command;
}

WorkerCommand defaultWorkerCommand()
{
    return workerCommandFor(currentExecutablePath());
}

std::string encodeScanResult(const ModuleScan& scan)
{
    // No indent is Miro's compact form: no newline outside string literals, which
    // is what keeps the result on one line.
    return std::string(scanResultPrefix) + Miro::toJSONString(scan);
}

void printScanResult(const ModuleScan& scan)
{
    const auto line = encodeScanResult(scan);

    std::fputc('\n', stdout);
    std::fputs(line.c_str(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

std::optional<ModuleScan> decodeScanResult(std::string_view workerOutput)
{
    const auto marker = workerOutput.rfind(scanResultPrefix);

    if (marker == std::string_view::npos)
        return std::nullopt;

    auto line = workerOutput.substr(marker + scanResultPrefix.size());

    if (const auto lineEnd = line.find_first_of("\r\n");
        lineEnd != std::string_view::npos)
        line = line.substr(0, lineEnd);

    try
    {
        // Json::parse first: createFromJSONString answers an all-defaults scan for
        // malformed input, which would read as a module with no classes.
        auto scan =
            Miro::createFromJSON<ModuleScan>(Miro::Json::parse(std::string(line)));

        if (scan.path.empty())
            return std::nullopt;

        return scan;
    }
    catch (...)
    {
        return std::nullopt;
    }
}

namespace
{
// argv without argv[0]; a null entry reads as empty.
Vector<std::string> argumentsOf(int argc, char** argv)
{
    auto arguments = Vector<std::string>();

    for (auto i = 1; i < argc; ++i)
        arguments.add(argv[i] != nullptr ? std::string(argv[i]) : std::string());

    return arguments;
}

// The command line eacp stored (runConsole, setCommandLineArgs), without the
// executable.
Vector<std::string> storedArguments()
{
    const auto& stored = eacp::Apps::getAppEnvironment().commandLineArgs;
    auto arguments = Vector<std::string>();

    for (auto i = 1; i < static_cast<int>(stored.size()); ++i)
        arguments.add(stored[i]);

    return arguments;
}

std::optional<std::string> targetIn(const Vector<std::string>& arguments)
{
    const auto count = static_cast<int>(arguments.size());

    for (auto i = 0; i < count; ++i)
    {
        if (arguments[i] != scanArgument)
            continue;

        if (i + 1 < count)
            return arguments[i + 1];

        return std::string();
    }

    return std::nullopt;
}

std::optional<int> runWorkerFor(const Vector<std::string>& arguments,
                                const FormatManager& formats)
{
    const auto requested = targetIn(arguments);

    if (!requested.has_value())
        return std::nullopt;

    if (requested->empty())
    {
        std::fprintf(stderr,
                     "%.*s needs a plugin to scan\n",
                     static_cast<int>(scanArgument.size()),
                     scanArgument.data());

        return static_cast<int>(ScanWorkerExit::missingTarget);
    }

    prepareWorkerProcess();

    const auto outcome = formats.scanTarget(*requested);

    if (outcome.request != TargetRequest::ok)
    {
        std::fprintf(stderr, "%s\n", outcome.message.c_str());
        return static_cast<int>(exitFor(outcome.request));
    }

    printScanResult(outcome.scan);
    return static_cast<int>(ScanWorkerExit::ok);
}
} // namespace

std::optional<std::string> requestedScanTarget()
{
    return targetIn(storedArguments());
}

std::optional<std::string> requestedScanTarget(int argc, char** argv)
{
    return targetIn(argumentsOf(argc, argv));
}

std::optional<int> runScanWorkerIfRequested(const FormatManager& formats)
{
    return runWorkerFor(storedArguments(), formats);
}

std::optional<int>
    runScanWorkerIfRequested(int argc, char** argv, const FormatManager& formats)
{
    return runWorkerFor(argumentsOf(argc, argv), formats);
}

} // namespace MakeASound::Hosting
