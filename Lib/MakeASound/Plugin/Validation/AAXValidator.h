#pragma once

#include "Pluginval.h"

#include <string_view>

// Avid's aaxval dish, driven through DigiShell's stdin. AAXValidator.cpp is the
// platform-free half (the scripts and reading dsh's answers); AAXValidator-macOS.cpp
// runs dsh, AAXValidator-Default.cpp fails every .aaxplugin.
namespace MakeASound::Pluginval
{
// A test aaxval lists that is never run, and why: the ones that fail outright for
// a native plugin with no page table, rather than reporting E_CANCELED (not
// applicable) as test.page_table.automation_list does. Pinned against the
// 2024.6.0 validator.
struct SkippedAAXTest
{
    std::string_view id;
    std::string_view reason;
};

const Vector<SkippedAAXTest>& skippedAAXTests();
bool isSkippedAAXTest(std::string_view id);

// One YAML document dsh printed between "---" and "...": its message type, the
// test id a runtest answer names, every result_status in it, the ids a listtests
// answer lists and a cmd_failed answer's reason.
struct DshAnswer
{
    std::string messageType;
    std::string id;
    Vector<std::string> statuses;
    Vector<std::string> listedIds;
    std::string failure;
};

Vector<DshAnswer> parseDshAnswers(std::string_view output);

// The ids a "listtests {detail: min}" answer lists, in order.
Vector<std::string> parseTestIds(const DshAnswer& answer);

std::string listTestsScript();

// Loads aaxval, runs each test on `bundle` (an absolute path holding no double
// quote, since DigiShell has no escaping) and exits: one answer per line.
std::string runTestsScript(const std::string& bundle,
                           const Vector<std::string>& tests);

// E_COMPLETED_PASS, or E_CANCELED: the validator found the test inapplicable.
bool isPassingStatus(std::string_view status);

// Matches each test to its answer, in the order the script ran them, and writes a
// PASS, N/A, FAIL or SKIP line per test into the result's log.
void judgeAAXRun(Result& result,
                 const Vector<std::string>& tests,
                 const Vector<DshAnswer>& answers);

Result validateAAX(const eacp::FilePath& bundle, const Options& options);
} // namespace MakeASound::Pluginval
