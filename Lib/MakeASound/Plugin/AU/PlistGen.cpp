// MakeASoundAUPlistGen: loads a <Name>-AU module and has it write its own
// Info.plist from describeModule(), run after every link of the module.
#include <dlfcn.h>

#include <cstdio>

namespace
{
using WritePlist = int (*)(const char*, const char*, const char*, const char*);
}

int main(int argc, char** argv)
{
    if (argc != 6)
    {
        std::fprintf(stderr,
                     "usage: %s <module binary> <bundle name> <bundle id> "
                     "<executable> <output plist>\n",
                     argv[0]);
        return 1;
    }

    // Never closed: unloading a module that started threads is not worth the risk.
    auto* module = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);

    if (module == nullptr)
    {
        std::fprintf(stderr, "%s\n", dlerror());
        return 1;
    }

    auto write =
        reinterpret_cast<WritePlist>(dlsym(module, "MakeASoundAUWritePlist"));

    if (write == nullptr)
    {
        std::fprintf(stderr, "%s\n", dlerror());
        return 1;
    }

    if (write(argv[2], argv[3], argv[4], argv[5]) != 0)
    {
        std::fprintf(stderr, "%s: cannot write %s\n", argv[1], argv[5]);
        return 1;
    }

    return 0;
}
