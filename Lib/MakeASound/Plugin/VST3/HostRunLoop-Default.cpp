#include "VST3Common.h"
#include "HostRunLoop.h"

namespace MakeASound::VST3
{

OwningPointer<HostRunLoop> attachHostRunLoop(FUnknown*)
{
    return {};
}

} // namespace MakeASound::VST3
