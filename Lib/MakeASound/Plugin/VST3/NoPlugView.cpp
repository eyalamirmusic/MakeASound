#include "VST3Common.h"
#include "PlugViewFactory.h"

namespace MakeASound::VST3
{

Steinberg::IPlugView* createPlugView(FUnknown&, Plugin&)
{
    return nullptr;
}

void releaseViewDevices()
{
}

} // namespace MakeASound::VST3
