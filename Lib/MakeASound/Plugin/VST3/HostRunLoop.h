#pragma once

#include "VST3Common.h"
#include "../../Common/Common.h"

namespace MakeASound::VST3
{

// Pumps the message loop from a run loop the host hands over, for as long as it
// lives. Linux's IRunLoop is the one platform run loop VST3 passes around, as the
// factory's host context and as a view's frame; HostRunLoop-Linux.cpp registers
// messageLoopFd() with it, HostRunLoop-Default.cpp has nothing to attach to.
class HostRunLoop
{
public:
    virtual ~HostRunLoop() = default;
};

// Null when `context` offers no run loop.
OwningPointer<HostRunLoop> attachHostRunLoop(FUnknown* context);

} // namespace MakeASound::VST3
