// The Linux VST3 default folders: VST3_PATH's entries first, as JUCE has them.

#include <MakeASound/Hosting/MakeASoundHosting.h>

#include <eacp/Core/Utils/Environment.h>

#include <NanoTest/NanoTest.h>

#include <filesystem>
#include <optional>
#include <string>

namespace HostingVST3FoldersTests
{
using namespace nano;
using namespace MakeASound::Hosting;

namespace
{
namespace fs = std::filesystem;

class ScopedVariable
{
public:
    ScopedVariable(const char* nameToUse, const std::string& value)
        : name(nameToUse)
        , previous(eacp::getEnv(nameToUse))
    {
        eacp::setEnv(name, value);
    }

    ~ScopedVariable()
    {
        if (previous)
            eacp::setEnv(name, *previous);
        else
            eacp::unsetEnv(name);
    }

    ScopedVariable(const ScopedVariable&) = delete;
    ScopedVariable& operator=(const ScopedVariable&) = delete;

private:
    const char* name;
    std::optional<std::string> previous;
};

auto tPath = test("HostingVST3Scan/vst3PathComesFirstOnLinux") = []
{
    const auto root = fs::temp_directory_path() / "MakeASoundHostingVST3Folders";
    fs::remove_all(root);
    fs::create_directories(root / "First");
    fs::create_directories(root / "Second");

    const auto first = (root / "First").generic_string();
    const auto second = (root / "Second").generic_string();
    const auto missing = (root / "Missing").generic_string();

    {
        const auto variable = ScopedVariable(
            "VST3_PATH", first + "::" + missing + ":" + second + "/:" + first);
        const auto folders = VST3Format().defaultSearchFolders();

        check(folders.size() >= 2);
        check(folders[0] == first);
        check(folders[1] == second);
        check(!folders.contains(missing));

        auto count = 0;

        for (const auto& folder: folders)
            count += folder == first ? 1 : 0;

        check(count == 1);
    }

    fs::remove_all(root);
};

auto tHome = test("HostingVST3Scan/vst3PathReadsALeadingTildeAsHome") = []
{
    const auto root = fs::temp_directory_path() / "MakeASoundHostingVST3FoldersHome";
    fs::remove_all(root);
    fs::create_directories(root / "Plugins");
    fs::create_directories(root / ".vst3");

    const auto plugins = (root / "Plugins").generic_string();
    const auto user = (root / ".vst3").generic_string();

    {
        const auto home = ScopedVariable("HOME", root.generic_string());
        const auto variable =
            ScopedVariable("VST3_PATH", "~/Plugins:~/.vst3/:~nobody:~");
        const auto folders = VST3Format().defaultSearchFolders();

        check(folders.size() >= 3);
        check(folders[0] == plugins);
        check(folders[1] == user);
        check(folders[2] == root.generic_string());

        auto count = 0;

        for (const auto& folder: folders)
        {
            count += folder == user ? 1 : 0;
            check(folder.find('~') == std::string::npos);
        }

        check(count == 1);
    }

    fs::remove_all(root);
};
} // namespace
} // namespace HostingVST3FoldersTests
