// A stand-in scan worker for the scan suites. The coordinator's job is coping
// with workers that misbehave, and a real one misbehaves only given a plugin that
// does, so the tests point it here and pick the behaviour on the command line:
//
//     FakeScanWorker --fake-worker <mode> [--delay <ms>] <target>
//
//   ok        prints plugin-like noise around a valid result line, exits 0
//   worker    the real worker, runScanWorkerIfRequested over a stub format
//             (needs --makeasound-scan before the target)
//   formats   the real worker over this platform's real formats, for the
//             fixture plugins (needs --makeasound-scan before the target)
//   old       answers with Tamber Plug's TAMBERPLUG_SCAN: marker, exits 0
//   garbage   prints junk and no result line, exits 0
//   crash     prints to stderr and exits 3
//   abort     dies on a signal, the way a plugin really crashes
//   hang      sleeps for a minute, waiting to be killed
//
// The target is always the last argument, which is how the coordinator builds a
// command line. A separate executable rather than a mode of the test binary,
// because NanoTest owns that main().

#include "HostingTestFormats.h"

#include <Miro/Reflection/Serialize.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

namespace
{
using namespace HostingTestFormats;

std::string argumentAfter(int argc, char** argv, const char* name)
{
    for (auto i = 1; i < argc - 1; ++i)
        if (argv[i] != nullptr && std::string(argv[i]) == name)
            return argv[i + 1];

    return {};
}
} // namespace

int main(int argc, char** argv)
{
    if (argc < 2)
        return 1;

    const auto mode = argumentAfter(argc, argv, "--fake-worker");
    const auto delay = argumentAfter(argc, argv, "--delay");
    const auto target = std::string(argv[argc - 1]);

    if (!delay.empty())
        std::this_thread::sleep_for(std::chrono::milliseconds(std::stoi(delay)));

    if (mode == "worker" || mode == "formats")
    {
        auto formats = FormatManager();

        if (mode == "formats")
            formats.addDefaultFormats();
        else
            formats.add(EA::makeOwned<StubFileFormat>());

        std::printf("[StubPlugin] chatter before the answer");

        if (const auto code = runScanWorkerIfRequested(argc, argv, formats))
            return *code;

        return 1;
    }

    if (mode == "hang")
    {
        std::this_thread::sleep_for(std::chrono::seconds(60));
        return 0;
    }

    if (mode == "crash")
    {
        std::fprintf(stderr, "fake worker refusing to scan %s\n", target.c_str());
        return 3;
    }

    if (mode == "abort")
        std::abort();

    if (mode == "garbage")
    {
        std::printf("not json at all\n{\"half\": \n");
        return 0;
    }

    const auto scan = StubFileFormat().scan(target);

    if (mode == "old")
    {
        std::printf("TAMBERPLUG_SCAN:%s\n", Miro::toJSONString(scan).c_str());
        return 0;
    }

    std::printf("[FakePlugin] loading %s\n", target.c_str());
    std::printf("%s\n", encodeScanResult(scan).c_str());
    std::printf("[FakePlugin] goodbye\n");

    return 0;
}
