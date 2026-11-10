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

// A module path is UTF-8; one that is not (bytes from a damaged cache, say) is a
// load that fails with a reason, never an exception into the host.
auto tInvalidUtf8 = test("VST3SDK/aPathThatIsNotUtf8FailsToLoadWithAReason") = []
{
    auto error = std::string();
    auto module = VST3::Hosting::Module::create("/no/such/\xff\xfe/Bad.vst3", error);

    check(module == nullptr);
    check(!error.empty());
};
} // namespace
} // namespace VST3SDKLoaderTests
