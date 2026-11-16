#include "Posix.h"

namespace MakeASound::CLAP
{

void attachHostLoop(HostLoop& loop, const clap_host* host) noexcept
{
    loop.host = host;
}

void detachHostLoop(HostLoop& loop) noexcept
{
    loop.host = nullptr;
}

const void* hostLoopExtension(const char*) noexcept
{
    return nullptr;
}

} // namespace MakeASound::CLAP
