#pragma once

#include <eacp/Graphics/Menu/Menu.h>
#include <eacp/Graphics/Window/Window.h>

#include <functional>
#include <string>

namespace MakeASound::Standalone
{

// What the desktop app offers beside the editor. No toggleTyping: the plugin
// takes no MIDI, so there is no typing keyboard to offer.
struct AppCommands
{
    std::string appName;
    std::function<void()> showSettings;
    std::function<void()> toggleTyping;
    std::function<bool()> isTyping;
    std::function<void()> resetPlugin;
    std::function<void()> resetAudioAndMidi;
};

// The one menu every desktop platform shows: menuToExtend's, with the commands
// below its first separator (macOS's "About" block) or at its top (above Quit
// in Linux's File menu).
eacp::Graphics::Menu appMenu(const AppCommands& commands);

// The menu the commands go into, one TU per platform: eacp's application menu
// on macOS and Linux (AppMenu-Default.cpp), a File menu of our own on Windows,
// whose application menu is an empty one named after the app
// (AppMenu-Windows.cpp).
eacp::Graphics::Menu menuToExtend(const std::string& appName);

// The menu in the window's menu bar: the system's on macOS and Windows, the one
// eacp draws in the window above the content on Linux.
void installAppMenu(const AppCommands& commands, eacp::Graphics::Window& window);

} // namespace MakeASound::Standalone
