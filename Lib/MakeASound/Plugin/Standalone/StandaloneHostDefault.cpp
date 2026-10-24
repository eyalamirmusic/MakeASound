// StandaloneHost's default constructor alone, out of the unity batch (see
// CMakeLists.txt): it is the one reference to describeModule(), so an app that
// builds its plugin itself and uses another constructor links no module.

#include "StandaloneHost.h"

namespace MakeASound::Standalone
{

StandaloneHost::StandaloneHost()
    : StandaloneHost(describeModule())
{
}

} // namespace MakeASound::Standalone
