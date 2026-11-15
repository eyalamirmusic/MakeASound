#pragma once

namespace PluginHost
{

// Before the first window: on Linux the X11 backend unless EACP_WINDOW_SYSTEM
// says otherwise, since a plugin's editor needs an X11 window to parent into
// (WindowSystem-Linux.cpp). Nothing elsewhere (WindowSystem-Default.cpp).
void chooseWindowSystem();

} // namespace PluginHost
