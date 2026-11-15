#include "../../Plugin/VST3/VST3Common.h"
#include "VST3RunLoop.h"

namespace MakeASound::Hosting
{

Steinberg::IPtr<VST3RunLoop> VST3RunLoop::create()
{
    return {};
}

} // namespace MakeASound::Hosting
