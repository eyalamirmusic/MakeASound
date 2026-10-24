#pragma once

#include "SettingsPanel.h"
#include "StandaloneHost.h"
#include "../Core/Editor.h"

namespace MakeASound::Standalone
{

// The default standalone app: a StandaloneHost with the module's first plugin and
// its editor, the settings panel picking its devices. What shows them is the
// platform's Shell, one TU each: on the desktop (StandaloneApp-Desktop.cpp) the
// editor window, the settings window, the menu bar and the typing keyboard; on
// iOS (StandaloneApp-iOS.cpp) one screen, the editor under a bar whose button
// swaps it for the settings panel and its resets. The host is the first member,
// so it goes last: the shell, its windows and the editor first, then the host's
// stream, then its wrapper, all on the main thread.
class StandaloneApp
{
public:
    StandaloneApp();
    ~StandaloneApp();

    StandaloneApp(const StandaloneApp&) = delete;
    StandaloneApp& operator=(const StandaloneApp&) = delete;

private:
    struct Shell;

    // What every shell does the same way, in StandaloneApp.cpp.
    static OwningPointer<Editor> createEditor(StandaloneHost& host);
    static void connectSettings(SettingsPanel& panel, StandaloneHost& host);
    static void resetAudioAndMidi(SettingsPanel& panel, StandaloneHost& host);

    // The shell's timer: host.poll(), the panel shown what it reopened.
    static void pollHost(SettingsPanel& panel, StandaloneHost& host);
    static constexpr auto pollHz = 1;

    StandaloneHost host;
    OwningPointer<Shell> shell;
};

} // namespace MakeASound::Standalone
