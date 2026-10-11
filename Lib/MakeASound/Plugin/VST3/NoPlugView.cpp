#include "VST3Common.h"
#include "PlugViewFactory.h"

namespace MakeASound::VST3
{

Steinberg::IPlugView* createPlugView(FUnknown&, Plugin&)
{
    return nullptr;
}

} // namespace MakeASound::VST3
