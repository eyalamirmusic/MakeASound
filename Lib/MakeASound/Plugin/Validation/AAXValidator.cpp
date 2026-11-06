#include "AAXValidator.h"

#include <algorithm>

namespace MakeASound::Pluginval
{
namespace
{
std::string_view trimmed(std::string_view text)
{
    auto first = text.find_first_not_of(" \t\r");

    if (first == std::string_view::npos)
        return {};

    auto last = text.find_last_not_of(" \t\r");
    return text.substr(first, last - first + 1);
}

std::string_view valueAfter(std::string_view line, std::string_view key)
{
    return trimmed(line.substr(key.size()));
}

std::string unquoted(std::string_view value)
{
    if (value.size() >= 2 && value.front() == value.back()
        && (value.front() == '"' || value.front() == '\''))
        value = value.substr(1, value.size() - 2);

    return std::string {value};
}
} // namespace

const Vector<SkippedAAXTest>& skippedAAXTests()
{
    static const auto skipped = Vector<SkippedAAXTest> {
        {"test.cycle_counts",
         "HDX only; its script loads the DAE dish and acquires a deck, which "
         "without HDX ends E_LOST about as often as E_CANCELED"},
        {"test.page_table.load",
         "fails (\"Failed to load page tables library\") for a plugin that "
         "ships no page table"}};
    return skipped;
}

bool isSkippedAAXTest(std::string_view id)
{
    const auto& skipped = skippedAAXTests();
    return std::any_of(skipped.begin(),
                       skipped.end(),
                       [&](const auto& test) { return test.id == id; });
}

Vector<DshAnswer> parseDshAnswers(std::string_view output)
{
    auto answers = Vector<DshAnswer> {};
    auto inDocument = false;

    while (!output.empty())
    {
        auto end = output.find('\n');
        auto line = output.substr(0, end);
        output = end == std::string_view::npos ? std::string_view {}
                                               : output.substr(end + 1);

        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);

        if (line == "---")
        {
            answers.emplace_back();
            inDocument = true;
            continue;
        }

        if (!inDocument)
            continue;

        if (line == "...")
        {
            inDocument = false;
            continue;
        }

        auto& answer = answers.back();
        auto text = trimmed(line);

        if (line.starts_with("message_type:"))
            answer.messageType = valueAfter(line, "message_type:");
        else if (line.starts_with("cmd_failure_reason:"))
            answer.failure = unquoted(valueAfter(line, "cmd_failure_reason:"));
        else if (line.starts_with("  id:"))
            answer.id = unquoted(valueAfter(line, "  id:"));
        else if (text.starts_with("result_status:"))
            answer.statuses.push_back(unquoted(valueAfter(text, "result_status:")));
        else if (text.starts_with("- id:"))
            answer.listedIds.push_back(unquoted(valueAfter(text, "- id:")));
    }

    return answers;
}

Vector<std::string> parseTestIds(const DshAnswer& answer)
{
    return answer.messageType == "cmd_result" ? answer.listedIds
                                              : Vector<std::string> {};
}

std::string listTestsScript()
{
    return "load_dish aaxval\nlisttests {detail: min, stringformat: yaml}\nexit\n";
}

std::string runTestsScript(const std::string& bundle,
                           const Vector<std::string>& tests)
{
    auto script = std::string {"load_dish aaxval\n"};

    for (const auto& test: tests)
        script += "runtest {test: " + test + ", path: \"" + bundle
                  + "\", stringformat: yaml, runmode: serial}\n";

    return script + "exit\n";
}

bool isPassingStatus(std::string_view status)
{
    return status == "E_COMPLETED_PASS" || status == "E_CANCELED";
}

void judgeAAXRun(Result& result,
                 const Vector<std::string>& tests,
                 const Vector<DshAnswer>& answers)
{
    auto summary = std::string {"aaxval:\n"};
    auto failed = 0;
    auto ran = 0;

    auto dishLoaded =
        !answers.empty() && answers.front().messageType == "cmd_result";

    if (!dishLoaded)
    {
        ++failed;
        summary += "  FAIL load_dish aaxval";

        if (!answers.empty() && !answers.front().failure.empty())
            summary += ": " + answers.front().failure;

        summary += "\n";
    }

    for (const auto& test: tests)
    {
        if (isSkippedAAXTest(test))
        {
            for (const auto& skipped: skippedAAXTests())
                if (skipped.id == test)
                    summary += "  SKIP " + test + ": " + std::string {skipped.reason}
                               + "\n";

            continue;
        }

        auto index = ++ran;

        if (!dishLoaded)
            continue;

        if (index >= answers.size())
        {
            ++failed;
            summary += "  FAIL " + test + ": no answer from dsh\n";
            continue;
        }

        const auto& answer = answers[index];

        if (answer.messageType != "cmd_result" || answer.statuses.empty())
        {
            ++failed;
            summary +=
                "  FAIL " + test + ": "
                + (answer.failure.empty() ? answer.messageType : answer.failure)
                + "\n";
            continue;
        }

        auto passed = std::all_of(answer.statuses.begin(),
                                  answer.statuses.end(),
                                  [](const auto& s) { return isPassingStatus(s); });

        auto applicable =
            std::any_of(answer.statuses.begin(),
                        answer.statuses.end(),
                        [](const auto& s) { return s != "E_CANCELED"; });

        auto statuses = std::string {};

        for (const auto& status: answer.statuses)
            statuses += (statuses.empty() ? "" : ", ") + status;

        if (!passed)
            ++failed;

        auto verdict = !passed ? "FAIL " : applicable ? "PASS " : "N/A  ";
        summary += "  " + std::string {verdict} + test + " (" + statuses + ")\n";
    }

    if (tests.empty())
    {
        ++failed;
        summary += "  FAIL aaxval listed no tests\n";
    }

    result.log += summary;
    result.passed = failed == 0;
    result.exitCode = failed;
}
} // namespace MakeASound::Pluginval
