// <Name>-Describe: an AUv3 app extension's Info.plist from describeModule(). An iOS
// binary can be neither loaded nor run on the build machine, so an iOS tree builds
// this in a host sub-build, linking the plugin's core built a second time for
// macOS from the same sources (see makeasound_add_plugin).
#include "Plist.h"

#include <cstdio>
#include <exception>
#include <string_view>

int main(int argc, char** argv)
{
    if (argc != 7)
    {
        std::fprintf(stderr,
                     "usage: %s <bundle name> <bundle id> <executable> "
                     "<principal class> <has view: 0|1> <output plist>\n",
                     argv[0]);
        return 1;
    }

    auto bundle = MakeASound::AU::AppExtensionPlist {
        .bundleName = argv[1],
        .bundleId = argv[2],
        .executable = argv[3],
        .principalClass = argv[4],
        .hasView = std::string_view(argv[5]) == "1"};

    try
    {
        if (MakeASound::AU::writeAppExtensionPlist(
                MakeASound::describeModule(), bundle, argv[6]))
            return 0;

        std::fprintf(stderr, "%s: cannot write %s\n", argv[0], argv[6]);
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "%s: %s\n", argv[0], e.what());
    }

    return 1;
}
