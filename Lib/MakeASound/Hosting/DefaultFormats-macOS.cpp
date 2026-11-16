#include "FormatManager.h"
#include "AU/AUFormat.h"
#include "DefaultFormatsCLAP.h"
#include "VST3/VST3Format.h"

namespace MakeASound::Hosting
{

// VST3 first, so a *.vst3 path is VST3's whatever else it looks like; CLAP, where
// this build hosts it, last.
void FormatManager::addDefaultFormats()
{
    add(EA::makeOwned<VST3Format>());
    add(EA::makeOwned<AUFormat>());
    addCLAPFormat(*this);
}

} // namespace MakeASound::Hosting
