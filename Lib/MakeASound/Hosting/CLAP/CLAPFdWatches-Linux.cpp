#include "CLAPFdWatches.h"

#include <eacp/Core/Threads/EventLoop-Linux.h>

#include <memory>
#include <utility>
#include <vector>

namespace MakeASound::Hosting
{

namespace
{
// One eacp FdWatcher per descriptor. eacp allows destroying a watcher from
// inside any watcher's callback, its own included, so a plugin may unregister or
// modify from its on_fd.
class EacpFdWatches : public CLAPFdWatches
{
public:
    explicit EacpFdWatches(Callback onReadyToUse)
        : onReady(std::move(onReadyToUse))
    {
    }

    ~EacpFdWatches() override { clear(); }

    bool add(int fd, clap_posix_fd_flags_t flags) override
    {
        if (fd < 0 || find(fd) != watches.end())
            return false;

        return watch(fd, flags);
    }

    bool modify(int fd, clap_posix_fd_flags_t flags) override
    {
        auto found = find(fd);

        if (found == watches.end())
            return false;

        // Taken out first, so a watcher dropped mid-change leaves the list
        // consistent.
        auto dropped = std::move(*found);
        watches.erase(found);
        return watch(fd, flags);
    }

    bool remove(int fd) override
    {
        auto found = find(fd);

        if (found == watches.end())
            return false;

        auto dropped = std::move(*found);
        watches.erase(found);
        return true;
    }

    void clear() noexcept override
    {
        auto dropped = std::move(watches);
        watches.clear();
    }

private:
    struct Watch
    {
        int fd = -1;
        std::unique_ptr<eacp::Threads::FdWatcher> watcher;
    };

    using Watches = std::vector<Watch>;

    Watches::iterator find(int fd)
    {
        auto found = watches.begin();

        while (found != watches.end() && found->fd != fd)
            ++found;

        return found;
    }

    bool watch(int fd, clap_posix_fd_flags_t flags)
    {
        // eacp's loop tells readable (or hung up) and nothing else.
        if ((flags & CLAP_POSIX_FD_READ) == 0)
            return false;

        auto watch = Watch();
        watch.fd = fd;
        watch.watcher = std::make_unique<eacp::Threads::FdWatcher>(
            fd, [this, fd] { onReady(fd, CLAP_POSIX_FD_READ); });

        watches.push_back(std::move(watch));
        return true;
    }

    Callback onReady;
    Watches watches;
};
} // namespace

OwningPointer<CLAPFdWatches> CLAPFdWatches::create(Callback onReady)
{
    return OwningPointer<CLAPFdWatches>(new EacpFdWatches(std::move(onReady)));
}

} // namespace MakeASound::Hosting
