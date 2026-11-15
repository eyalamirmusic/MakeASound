#include "AppCommands.h"

namespace MakeASound::Standalone
{

// eacp's application menu on Windows is an empty one named after the app, so
// the commands open a File menu of their own.
eacp::Graphics::Menu menuToExtend(const std::string&)
{
    return eacp::Graphics::Menu {"File"};
}

} // namespace MakeASound::Standalone
