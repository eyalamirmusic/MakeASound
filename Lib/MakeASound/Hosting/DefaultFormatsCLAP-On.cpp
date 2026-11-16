#include "DefaultFormatsCLAP.h"
#include "CLAP/CLAPFormat.h"

namespace MakeASound::Hosting
{

void addCLAPFormat(FormatManager& formats)
{
    formats.add(EA::makeOwned<CLAPFormat>());
}

} // namespace MakeASound::Hosting
