// BundleIdFixture: a plugin that does nothing, built twice by
// Tests/BundleIds/CMakeLists.txt, once with each format's identifier replaced
// and once with the defaults, so CheckBundles.cmake can read what the bundles
// were given. Neither is ever installed or loaded.

#include <MakeASound/Plugin/MakeASoundPlugin.h>

namespace
{
using namespace MakeASound;

struct FixturePlugin : Plugin
{
    std::string_view name() const override { return "BundleIdFixture"; }

    void prepare(const ProcessSpec&) override {}
    void process(ProcessContext&) noexcept override {}
};
} // namespace

namespace MakeASound
{

ModuleDescription describeModule()
{
    auto module = ModuleDescription {};
    module.vendor = "MakeASound Tests";
    module.manufacturerCode = "MkTs";
    module.plugins.add({.name = "BundleIdFixture",
                        .category = Category::Effect,
                        .pluginCode = "BdId",
                        .create = [] { return EA::makeOwned<FixturePlugin>(); }});
    return module;
}

} // namespace MakeASound
