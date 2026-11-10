// The vendored VST3 SDK's module loader links, wherever vst3sdk-hosting has one
// (not iOS): a missing module fails to load and says why. Nothing here opens a
// plugin.

#include <public.sdk/source/vst/hosting/module.h>

#include <NanoTest/NanoTest.h>

#include <string>

namespace VST3SDKLoaderTests
{
using namespace nano;

namespace
{
auto tMissingModule = test("VST3SDK/aMissingModuleFailsToLoadWithAReason") = []
{
    auto error = std::string();
    auto module =
        VST3::Hosting::Module::create("/no/such/folder/Missing.vst3", error);

    check(module == nullptr);
    check(!error.empty());
};

#if defined(_WIN32)
// A module path is UTF-8; one that is not (bytes from a damaged cache, say) is a
// load that fails with a reason, never an exception into the host. Windows only:
// the case checks the catch in the locally patched module_win32.cpp. The
// unpatched module_mac.mm hands such a path to CFBundleCreate, which crashes.
auto tInvalidUtf8 = test("VST3SDK/aPathThatIsNotUtf8FailsToLoadWithAReason") = []
{
    auto error = std::string();
    auto module = VST3::Hosting::Module::create("/no/such/\xff\xfe/Bad.vst3", error);

    check(module == nullptr);
    check(!error.empty());
};
#endif
} // namespace
} // namespace VST3SDKLoaderTests
