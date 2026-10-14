// The vendored VST3 SDK's module loader links, wherever vst3sdk-hosting has one
// (not iOS): a missing module fails to load and says why. Nothing here opens a
// plugin.

#include <public.sdk/source/vst/hosting/module.h>

#include <NanoTest/NanoTest.h>

#include <string>

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
} // namespace
