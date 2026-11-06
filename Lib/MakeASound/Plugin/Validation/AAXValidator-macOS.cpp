#include "AAXValidator.h"

#include <eacp/Core/Process/Process.h>
#include <eacp/Core/Utils/StdPath.h>

#include <sys/xattr.h>

#include <chrono>
#include <filesystem>

namespace MakeASound::Pluginval
{
namespace
{
namespace fs = std::filesystem;

struct DshRun
{
    bool launched = false;
    bool stalled = false;
    int exitCode = -1;
    std::string output;
};

// The script goes in whole and dsh answers each line in turn. A test prints
// nothing until it is done, and dsh can hang after a failed command, so a run
// that prints nothing for `stall` is killed with whatever it forked.
DshRun runDsh(const fs::path& dsh, const std::string& script, eacp::Time::MS stall)
{
    auto process = eacp::Processes::Process {dsh.string()};
    auto run = DshRun {.launched = process.launched()};

    if (!run.launched)
        return run;

    process.writeToInput(script);
    process.closeInput();

    auto printed = std::size_t {0};
    auto deadline = eacp::Time::Deadline {stall};

    while (process.isRunning())
    {
        eacp::Time::sleepMS(100);

        if (auto size = process.output().size(); size != printed)
        {
            printed = size;
            deadline = eacp::Time::Deadline {stall};
            continue;
        }

        if (deadline.expired())
        {
            run.stalled = true;
            eacp::Processes::run("/usr/bin/pkill",
                                 {"-KILL", "-P", std::to_string(process.id())});
            process.kill();
            break;
        }
    }

    run.exitCode = process.wait();
    run.output = process.output() + process.errorOutput();
    return run;
}

bool isQuarantined(const fs::path& file)
{
    return getxattr(file.c_str(), "com.apple.quarantine", nullptr, 0, 0, 0) >= 0;
}

std::string failureHint(const fs::path& dsh, const fs::path& tools)
{
    if (isQuarantined(dsh))
        return "dsh is quarantined: run xattr -dr com.apple.quarantine \""
               + tools.string() + "\"\n";

    return {};
}

std::string describe(const DshRun& run, const fs::path& dsh)
{
    if (!run.launched)
        return "could not launch " + dsh.string() + "\n";

    if (run.stalled)
        return "dsh printed nothing for too long and was killed\n";

    return {};
}
} // namespace

Result validateAAX(const eacp::FilePath& bundle, const Options& options)
{
    auto started = std::chrono::steady_clock::now();
    auto result = Result {.bundle = bundle};

    auto finish = [&]
    {
        auto elapsed = std::chrono::steady_clock::now() - started;
        result.seconds = std::chrono::duration<double>(elapsed).count();
        return result;
    };

    if (options.aaxTools.str().empty())
    {
        result.log = "AAX validation needs --aax-tools (or MAKEASOUND_AAX_TOOLS): "
                     "the AAX Validator folder\n";
        return finish();
    }

    auto tools = fs::absolute(eacp::toStdPath(options.aaxTools));
    auto dsh = tools / "CommandLineTools" / "dsh";
    auto error = std::error_code {};

    if (!fs::is_regular_file(dsh, error))
    {
        result.log = "no CommandLineTools/dsh in " + tools.string() + "\n";
        return finish();
    }

    auto path = eacp::toStdPath(bundle);

    if (!path.has_filename())
        path = path.parent_path();

    auto target = fs::absolute(path).lexically_normal().string();

    if (target.find('"') != std::string::npos)
    {
        result.log =
            "DigiShell cannot take a path holding a double quote: " + target + "\n";
        return finish();
    }

    auto listing = runDsh(dsh, listTestsScript(), options.aaxStallTimeout);
    auto listed = parseDshAnswers(listing.output);
    auto tests =
        listed.size() >= 2 ? parseTestIds(listed[1]) : Vector<std::string> {};

    if (tests.empty())
    {
        result.log = listing.output + describe(listing, dsh)
                     + "aaxval listed no tests\n" + failureHint(dsh, tools);
        return finish();
    }

    auto runnable = Vector<std::string> {};

    for (const auto& test: tests)
        if (!isSkippedAAXTest(test))
            runnable.push_back(test);

    auto run =
        runDsh(dsh, runTestsScript(target, runnable), options.aaxStallTimeout);

    auto answers = parseDshAnswers(run.output);
    auto dishLoaded =
        !answers.empty() && answers.front().messageType == "cmd_result";

    result.log = run.output + describe(run, dsh);
    judgeAAXRun(result, tests, answers);

    if (!dishLoaded)
        result.log += failureHint(dsh, tools);

    if (!result.passed && run.launched && !run.stalled && run.exitCode != 0)
        result.exitCode = run.exitCode;

    return finish();
}
} // namespace MakeASound::Pluginval
