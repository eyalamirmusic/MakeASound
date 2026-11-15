#include "AppCommands.h"

namespace MakeASound::Standalone
{
namespace Graphics = eacp::Graphics;

namespace
{
auto afterFirstSeparator(const Graphics::Menu& menu) noexcept
{
    auto at = decltype(menu.items.size()) {};

    for (const auto& item: menu.items)
    {
        ++at;

        if (item.isSeparator)
            return at;
    }

    return decltype(at) {};
}
} // namespace

Graphics::Menu appMenu(const AppCommands& commands)
{
    using Graphics::commandKey;
    using Graphics::MenuItem;

    auto menu = menuToExtend(commands.appName);
    auto at = afterFirstSeparator(menu);

    menu.items.insert(at++,
                      MenuItem::withAction("Audio / MIDI Settings…",
                                           commands.showSettings,
                                           commandKey(",")));

    if (commands.toggleTyping)
        menu.items.insert(at++,
                          MenuItem::withCheckableAction("Computer MIDI Keyboard",
                                                        commands.toggleTyping,
                                                        commands.isTyping,
                                                        commandKey("k")));

    menu.items.insert(at++, MenuItem::separator());

    menu.items.insert(
        at++,
        MenuItem::withAction("Reset Plugin to Defaults", commands.resetPlugin));

    menu.items.insert(at++,
                      MenuItem::withAction("Reset Audio / MIDI Settings",
                                           commands.resetAudioAndMidi));

    if (at < menu.items.size())
        menu.items.insert(at, MenuItem::separator());

    return menu;
}

void installAppMenu(const AppCommands& commands, Graphics::Window& window)
{
    auto bar = Graphics::MenuBar {};
    bar.add(appMenu(commands));
    Graphics::setApplicationMenuBar(bar, window);
}

} // namespace MakeASound::Standalone
