#include "WindowSystem.h"

#include <eacp/Core/Utils/Environment.h>

namespace PluginHost
{

void chooseWindowSystem()
{
    if (!eacp::getEnv("EACP_WINDOW_SYSTEM"))
        eacp::setEnv("EACP_WINDOW_SYSTEM", "x11");
}

} // namespace PluginHost
