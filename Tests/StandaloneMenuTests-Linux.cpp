// The Linux standalone's menu: eacp's File menu with the app's commands above its
// Quit, one menu and one Quit, each item running its command. The bar that shows
// it is eacp's.

#include <MakeASound/Plugin/Standalone/AppCommands.h>

#include <NanoTest/NanoTest.h>

#include <algorithm>
#include <string>
#include <vector>

namespace StandaloneMenuTests
{
using namespace nano;
using namespace MakeASound::Standalone;

namespace
{
namespace Graphics = eacp::Graphics;

struct Commands
{
    explicit Commands(bool takesMidi)
    {
        actions.appName = "Menu Test";
        actions.showSettings = [this] { ++settings; };
        actions.resetPlugin = [this] { ++pluginResets; };
        actions.resetAudioAndMidi = [this] { ++audioResets; };

        if (takesMidi)
        {
            actions.toggleTyping = [this] { typing = !typing; };
            actions.isTyping = [this] { return typing; };
        }
    }

    AppCommands actions;
    int settings = 0;
    int pluginResets = 0;
    int audioResets = 0;
    bool typing = false;
};

// Each item's title, a separator as "-".
std::vector<std::string> titlesOf(const Graphics::Menu& menu)
{
    auto titles = std::vector<std::string> {};

    for (const auto& item: menu.items)
        titles.push_back(item.isSeparator ? std::string {"-"} : item.title);

    return titles;
}

const Graphics::MenuItem* itemNamed(const Graphics::Menu& menu,
                                    const std::string& title)
{
    for (const auto& item: menu.items)
        if (!item.isSeparator && item.title == title)
            return &item;

    return nullptr;
}

auto tFile = test("Standalone/linuxMenuIsFileWithTheCommandsAboveQuit") = []
{
    auto commands = Commands {true};
    auto menu = appMenu(commands.actions);

    check(menu.title == "File");

    auto expected = std::vector<std::string> {"Audio / MIDI Settings…",
                                              "Computer MIDI Keyboard",
                                              "-",
                                              "Reset Plugin to Defaults",
                                              "Reset Audio / MIDI Settings",
                                              "-",
                                              "Quit"};
    check(titlesOf(menu) == expected);

    auto* settings = itemNamed(menu, "Audio / MIDI Settings…");
    check(settings != nullptr && settings->shortcut.has_value()
          && settings->shortcut->key == ",");

    auto* typing = itemNamed(menu, "Computer MIDI Keyboard");
    check(typing != nullptr && typing->shortcut.has_value()
          && typing->shortcut->key == "k");
};

auto tNoMidi = test("Standalone/linuxMenuLeavesOutTheKeyboardWithoutMidi") = []
{
    auto commands = Commands {false};
    auto titles = titlesOf(appMenu(commands.actions));

    check(std::ranges::count(titles, "Computer MIDI Keyboard") == 0);
    check(std::ranges::count(titles, "Quit") == 1);
    check(titles.front() == "Audio / MIDI Settings…");
    check(titles.back() == "Quit");
};

auto tRuns = test("Standalone/linuxMenuItemsRunTheirCommands") = []
{
    auto commands = Commands {true};
    auto menu = appMenu(commands.actions);

    itemNamed(menu, "Audio / MIDI Settings…")->action();
    itemNamed(menu, "Reset Plugin to Defaults")->action();
    itemNamed(menu, "Reset Audio / MIDI Settings")->action();
    check(commands.settings == 1);
    check(commands.pluginResets == 1);
    check(commands.audioResets == 1);

    // The tick reads the state each time it is asked, as the bar asks on open.
    auto* typing = itemNamed(menu, "Computer MIDI Keyboard");
    check(typing->isChecked && !typing->isChecked());
    typing->action();
    check(commands.typing && typing->isChecked());
};
} // namespace
} // namespace StandaloneMenuTests
