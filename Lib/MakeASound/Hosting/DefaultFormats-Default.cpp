#include "FormatManager.h"
#include "DefaultFormatsCLAP.h"
#include "VST3/VST3Format.h"

namespace MakeASound::Hosting
{

void FormatManager::addDefaultFormats()
{
    add(EA::makeOwned<VST3Format>());
    addCLAPFormat(*this);
}

} // namespace MakeASound::Hosting
