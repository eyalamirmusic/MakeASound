// Every example bundle through its validator: pluginval at strictness 10 for a
// .vst3, auval -strict for a .component where the AU format was built. It needs the
// network on the first run and ~20 s a bundle, so it runs only when
// MAKEASOUND_PLUGINVAL is set ("nogui" skips pluginval's editor tests) and passes
// otherwise. The cases after it are the AAX half's bundle finding and its reading
// of dsh's answers, which need neither the validator nor a bundle.

#include <MakeASound/Plugin/Validation/Pluginval.h>
#include <MakeASound/Plugin/Validation/AAXValidator.h>

#include <NanoTest/NanoTest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>

namespace PluginvalTests
{
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

#ifdef MAKEASOUND_AU_DIR
    auto components = Pluginval::findBundles(MAKEASOUND_AU_DIR);
    check(!components.empty());

    for (auto& component: components)
        bundles.push_back(std::move(component));
#endif

    auto pluginval = Pluginval::fetch(options);

    for (const auto& bundle: bundles)
    {
        auto result = Pluginval::validate(pluginval, bundle, options);

        if (!result.passed)
            std::cout << result.log << '\n';

        check(result.passed);
    }
};

namespace
{
struct ScratchFolder
{
    ScratchFolder()
    {
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }

    ~ScratchFolder() { std::filesystem::remove_all(path); }

    void add(const std::string& name, bool directory = true) const
    {
        if (directory)
            std::filesystem::create_directories(path / name);
        else
            std::ofstream {path / name} << "x";
    }

    std::filesystem::path path =
        std::filesystem::temp_directory_path() / "MakeASoundPluginvalTests";
};

// What dsh 2024.6.0 printed for these commands, trimmed.
constexpr auto listTestsOutput = R"(DigiShell v24.9.0x14
Avid Technology, Inc.
dsh> load_dish aaxval
---
cmd_result:
  loaded_dishes_count: 1
message_type: cmd_result
...
dsh> listtests {detail: min, stringformat: yaml}
---
cmd_result:
  test_info:
    - id: info.productids
    - id: test.cycle_counts
    - id: test.data_model
message_type: cmd_result
...
dsh> exit
---
cmd_result: exiting
message_type: cmd_result
...
)";

constexpr auto runTestsOutput = R"(dsh> load_dish aaxval
---
cmd_result:
  loaded_dishes_count: 1
message_type: cmd_result
...
dsh> runtest {test: info.productids, path: "/a b/G.aaxplugin", stringformat: yaml}
about to exec...
 fork successful continuing parent process
---
cmd_result:
  bundle_path: /a b/G.aaxplugin
  id: info.productids
  result_ref: r1
  results:
    - connection_id: 1.000000
      result:
        logs:
          - Test stage completed and PASSED
      result_status: E_COMPLETED_PASS
      summary:
        successful: 1
        total: 1
message_type: cmd_result
...
dsh> runtest {test: test.page_table.automation_list, path: "/a b/G.aaxplugin"}
---
cmd_result:
  id: test.page_table.automation_list
  results:
    - connection_id: 2.000000
      result_status: E_CANCELED
message_type: cmd_result
...
dsh> runtest {test: test.data_model, path: "/a b/G.aaxplugin", stringformat: yaml}
---
cmd_failure_reason: AAXVal_CPlugInManager::CPrivateData::UpdateAbsoluteAAXPlugInPaths() called for non-existant path
message_type: cmd_failed
...
)";
} // namespace

auto pluginvalRecognisesAAX = test("Pluginval/recognisesAAXBundles") = []
{
    check(Pluginval::isAAX(eacp::FilePath {"build/AAX/MakeASound Gain.aaxplugin"}));
    check(Pluginval::isAAX(eacp::FilePath {"build/AAX/Gain.aaxplugin/"}));
    check(!Pluginval::isAAX(eacp::FilePath {"build/VST3/Gain.vst3"}));
    check(!Pluginval::isAudioUnit(eacp::FilePath {"Gain.aaxplugin"}));
};

auto pluginvalFindsEveryFormat = test("Pluginval/findBundlesFindsEveryFormat") = []
{
    auto folder = ScratchFolder {};
    folder.add("B.aaxplugin");
    folder.add("A.vst3");
    folder.add("C.component");
    folder.add("Other");
    folder.add("notes.txt", false);

    auto bundles = Pluginval::findBundles(folder.path);
    check(bundles.size() == 3);

    if (bundles.size() == 3)
    {
        check(Pluginval::isAAX(bundles[1]));
        check(Pluginval::isAudioUnit(bundles[2]));
    }
};

auto pluginvalAAXNeedsTools = test("Pluginval/aaxWithoutToolsFails") = []
{
    auto options = Pluginval::Options {};
    options.aaxTools = {};

    auto result =
        Pluginval::validate({}, eacp::FilePath {"/nowhere/Gain.aaxplugin"}, options);

    check(!result.passed);
    check(result.log.find("AAX validation") != std::string::npos);
};

auto pluginvalParsesListTests = test("Pluginval/aaxvalListTestsParsed") = []
{
    auto answers = Pluginval::parseDshAnswers(listTestsOutput);
    check(answers.size() == 3);

    auto ids = Pluginval::parseTestIds(answers[1]);
    check(ids.size() == 3);
    check(ids.size() == 3 && ids[0] == "info.productids"
          && ids[2] == "test.data_model");
};

auto pluginvalScriptsQuoteThePath = test("Pluginval/aaxvalScriptQuotesThePath") = []
{
    auto script = Pluginval::runTestsScript("/a b/G.aaxplugin", {"test.data_model"});

    check(script
          == "load_dish aaxval\n"
             "runtest {test: test.data_model, path: \"/a b/G.aaxplugin\", "
             "stringformat: yaml, runmode: serial}\n"
             "exit\n");
};

auto pluginvalJudgesRuns = test("Pluginval/aaxvalVerdicts") = []
{
    auto answers = Pluginval::parseDshAnswers(runTestsOutput);
    check(answers.size() == 4);
    check(answers.size() == 4 && answers[1].id == "info.productids");
    check(answers.size() == 4 && answers[3].messageType == "cmd_failed");

    // As listtests lists them: the skipped one has no answer of its own.
    auto tests = MakeASound::Vector<std::string> {
        "info.productids", "test.cycle_counts", "test.page_table.automation_list"};
    auto passing = Pluginval::Result {};
    Pluginval::judgeAAXRun(passing, tests, answers);
    check(passing.passed);
    check(passing.log.find("SKIP test.cycle_counts") != std::string::npos);
    check(passing.log.find("N/A  test.page_table.automation_list")
          != std::string::npos);

    tests.push_back("test.data_model");
    auto failing = Pluginval::Result {};
    Pluginval::judgeAAXRun(failing, tests, answers);
    check(!failing.passed);
    check(failing.exitCode == 1);
    check(failing.log.find("non-existant path") != std::string::npos);

    tests.push_back("test.parameters");
    auto unanswered = Pluginval::Result {};
    Pluginval::judgeAAXRun(unanswered, tests, answers);
    check(unanswered.exitCode == 2);
    check(unanswered.log.find("no answer from dsh") != std::string::npos);

    check(!Pluginval::isPassingStatus("E_COMPLETED_FAIL"));
    check(!Pluginval::isPassingStatus("E_TIMEOUT"));
    check(!Pluginval::isPassingStatus("E_LOST"));
    check(Pluginval::isSkippedAAXTest("test.page_table.load"));
    check(!Pluginval::isSkippedAAXTest("test.page_table.automation_list"));
};

auto pluginvalNoDish = test("Pluginval/aaxvalFailsWhenTheDishDoesNotLoad") = []
{
    auto answers = Pluginval::parseDshAnswers("---\n"
                                              "cmd_failure_reason: no dish\n"
                                              "message_type: cmd_failed\n"
                                              "...\n");
    auto result = Pluginval::Result {};
    Pluginval::judgeAAXRun(result, {"test.data_model"}, answers);
    check(!result.passed);
    check(result.log.find("FAIL load_dish aaxval: no dish") != std::string::npos);
};
} // namespace PluginvalTests
