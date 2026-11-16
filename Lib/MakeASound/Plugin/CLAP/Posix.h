#pragma once

#include <clap/clap.h>

namespace MakeASound::CLAP
{

// The message loop pumped from the host's own, so what callOnMessageThread
// queues runs under a CLAP host. On Linux (Posix-Linux.cpp) init registers
// messageLoopFd() through the host's clap.posix-fd-support, or, where the host
// has none, a timer of about 16 ms through clap.timer-support, and the plugin's
// on_fd or on_timer runs one pass of the loop; destroy unregisters. Elsewhere
// (Posix-Default.cpp) the platform's own message loop serves us, and all of
// this is a no-op. Main thread.
struct HostLoop
{
    const clap_host* host = nullptr;
    const clap_host_posix_fd_support* fdSupport = nullptr;
    const clap_host_timer_support* timerSupport = nullptr;
    int fd = -1;
    clap_id timer = CLAP_INVALID_ID;
};

void attachHostLoop(HostLoop& loop, const clap_host* host) noexcept;
void detachHostLoop(HostLoop& loop) noexcept;

// clap.posix-fd-support or clap.timer-support, where this platform pumps the
// loop through them; null otherwise.
const void* hostLoopExtension(const char* id) noexcept;

} // namespace MakeASound::CLAP
