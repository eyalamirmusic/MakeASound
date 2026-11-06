#include "FormatManager.h"
#include "AU/AUFormat.h"
#include "VST3/VST3Format.h"

namespace MakeASound::Hosting
{

// VST3 first, so a *.vst3 path is VST3's whatever else it looks like.
void FormatManager::addDefaultFormats()
{
    add(EA::makeOwned<VST3Format>());
    add(EA::makeOwned<AUFormat>());
}

} // namespace MakeASound::Hosting
