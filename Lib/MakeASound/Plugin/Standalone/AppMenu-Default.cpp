#include "AppCommands.h"

namespace MakeASound::Standalone
{

// macOS's application menu (About, Hide, Quit), or on Linux eacp's File menu
// with Quit.
eacp::Graphics::Menu menuToExtend(const std::string& appName)
{
    return eacp::Graphics::standardApplicationMenu(appName);
}

} // namespace MakeASound::Standalone
