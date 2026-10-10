// Both example bundles through Tracktion's pluginval at strictness 10. It needs the
// network on the first run and ~20 s a bundle, so it runs only when
// MAKEASOUND_PLUGINVAL is set ("nogui" skips the editor tests) and passes otherwise.

#include <MakeASound/Plugin/Validation/Pluginval.h>

#include <NanoTest/NanoTest.h>

#include <cstdlib>
#include <iostream>
#include <string_view>

using namespace nano;
namespace Pluginval = MakeASound::Pluginval;

auto pluginvalExampleBundles =
    test("Pluginval/exampleBundlesPassAtStrictness10") = []
{
    auto* setting = std::getenv("MAKEASOUND_PLUGINVAL");

    if (setting == nullptr)
        return;

    auto options = Pluginval::Options {};
    options.guiTests = std::string_view {setting} != "nogui";

    auto bundles = Pluginval::findBundles(MAKEASOUND_VST3_DIR);
    check(!bundles.empty());

    auto pluginval = Pluginval::fetch(options);

    for (const auto& bundle: bundles)
    {
        auto result = Pluginval::validate(pluginval, bundle, options);

        if (!result.passed)
            std::cout << result.log << '\n';

        check(result.passed);
    }
};
