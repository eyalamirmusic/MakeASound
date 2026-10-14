// WebEditorFixture: a plugin that does nothing, built twice by
// Tests/WebEditorBuild/CMakeLists.txt, each core with a page of its own, so
// CheckPage.cmake can read what each page's build was given. FIXTURE_CODE tells
// the two apart. Neither is ever installed or loaded.

#include <MakeASound/Plugin/MakeASoundPlugin.h>

namespace
{
using namespace MakeASound;

struct FixturePlugin : Plugin
{
    std::string_view name() const override { return "WebEditorFixture"; }

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
    module.plugins.add({.name = "WebEditorFixture",
                        .category = Category::Effect,
                        .pluginCode = FIXTURE_CODE,
                        .create = [] { return EA::makeOwned<FixturePlugin>(); }});
    return module;
}

} // namespace MakeASound
