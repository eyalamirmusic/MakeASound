#include "ScanWorker.h"
#include "ExecutablePath.h"
#include "WorkerProcess.h"

#include <Miro/Reflection/Serialize.h>

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

std::optional<std::string> requestedScanTarget(int argc, char** argv)
{
    for (auto i = 1; i < argc; ++i)
    {
        if (argv[i] == nullptr || scanArgument != argv[i])
            continue;

        if (i + 1 < argc && argv[i + 1] != nullptr)
            return std::string(argv[i + 1]);

        return std::string();
    }

    return std::nullopt;
}

std::optional<int>
    runScanWorkerIfRequested(int argc, char** argv, const FormatManager& formats)
{
    const auto requested = requestedScanTarget(argc, argv);

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

} // namespace MakeASound::Hosting
