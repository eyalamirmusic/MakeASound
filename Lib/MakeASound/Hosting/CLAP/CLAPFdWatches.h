#pragma once

#include "../../Common/Common.h"

#include <clap/ext/posix-fd-support.h>

#include <functional>

namespace MakeASound::Hosting
{

// The descriptors a hosted CLAP plugin registers through clap.posix-fd-support,
// watched on the host's message loop. Linux only (CLAPFdWatches-Linux.cpp, over
// eacp's FdWatcher): create() is null on every other platform
// (CLAPFdWatches-Default.cpp), where the host offers no such extension.
//
// Readiness to read is all eacp's loop reports, so a descriptor is watched only
// when the plugin asks for CLAP_POSIX_FD_READ, and every call says READ.
// Message thread throughout. Internal to the CLAP format.
class CLAPFdWatches
{
public:
    using Callback = std::function<void(int fd, clap_posix_fd_flags_t flags)>;

    static OwningPointer<CLAPFdWatches> create(Callback onReady);

    virtual ~CLAPFdWatches() = default;

    // register_fd, modify_fd and unregister_fd's answers.
    virtual bool add(int fd, clap_posix_fd_flags_t flags) = 0;
    virtual bool modify(int fd, clap_posix_fd_flags_t flags) = 0;
    virtual bool remove(int fd) = 0;

    // Stops watching everything, for an instance going away whatever it left
    // registered.
    virtual void clear() noexcept = 0;
};

} // namespace MakeASound::Hosting
