#include "MessageThread.h"

#include <eacp/Core/Threads/EventLoop-Linux.h>

namespace MakeASound
{

int messageLoopFd()
{
    return eacp::Threads::getEventLoopFd();
}

void pumpMessageLoop()
{
    eacp::Threads::pumpEventLoop();
}

} // namespace MakeASound
