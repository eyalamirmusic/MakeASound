#include "Posix.h"
#include "../Realtime/MessageThread.h"

#include <cstring>

namespace MakeASound::CLAP
{

namespace
{
// Every instance registers the same descriptor with its own host; a pass with
// nothing queued returns at once.
void pump() noexcept
{
    try
    {
        pumpMessageLoop();
    }
    catch (...)
    {
    }
}

void onFd(const clap_plugin*, int, clap_posix_fd_flags_t) noexcept
{
    pump();
}

void onTimer(const clap_plugin*, clap_id) noexcept
{
    pump();
}

constexpr auto fdExtension = clap_plugin_posix_fd_support {onFd};
constexpr auto timerExtension = clap_plugin_timer_support {onTimer};

constexpr auto timerPeriodMs = uint32_t {16};

template <typename Extension>
const Extension* hostExtensionOf(const clap_host* host, const char* id) noexcept
{
    if (host == nullptr || host->get_extension == nullptr)
        return nullptr;

    return static_cast<const Extension*>(host->get_extension(host, id));
}
} // namespace

void attachHostLoop(HostLoop& loop, const clap_host* host) noexcept
{
    loop.host = host;

    auto fd = -1;

    try
    {
        fd = messageLoopFd();
    }
    catch (...)
    {
    }

    if (host == nullptr || fd < 0)
        return;

    loop.fdSupport =
        hostExtensionOf<clap_host_posix_fd_support>(host, CLAP_EXT_POSIX_FD_SUPPORT);

    if (loop.fdSupport != nullptr && loop.fdSupport->register_fd != nullptr
        && loop.fdSupport->register_fd(host, fd, CLAP_POSIX_FD_READ))
    {
        loop.fd = fd;
        return;
    }

    loop.timerSupport =
        hostExtensionOf<clap_host_timer_support>(host, CLAP_EXT_TIMER_SUPPORT);

    auto timer = clap_id {CLAP_INVALID_ID};

    if (loop.timerSupport != nullptr && loop.timerSupport->register_timer != nullptr
        && loop.timerSupport->register_timer(host, timerPeriodMs, &timer))
        loop.timer = timer;
}

void detachHostLoop(HostLoop& loop) noexcept
{
    if (loop.fd >= 0 && loop.fdSupport->unregister_fd != nullptr)
        loop.fdSupport->unregister_fd(loop.host, loop.fd);

    if (loop.timer != CLAP_INVALID_ID
        && loop.timerSupport->unregister_timer != nullptr)
        loop.timerSupport->unregister_timer(loop.host, loop.timer);

    loop = {};
}

const void* hostLoopExtension(const char* id) noexcept
{
    if (std::strcmp(id, CLAP_EXT_POSIX_FD_SUPPORT) == 0)
        return &fdExtension;

    if (std::strcmp(id, CLAP_EXT_TIMER_SUPPORT) == 0)
        return &timerExtension;

    return nullptr;
}

} // namespace MakeASound::CLAP
