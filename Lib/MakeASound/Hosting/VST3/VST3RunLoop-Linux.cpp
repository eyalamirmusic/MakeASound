#include "../../Plugin/VST3/VST3Common.h"
#include "VST3RunLoop.h"
#include "../../Common/Common.h"

#include <base/source/fobject.h>

#include <eacp/Core/Threads/EventLoop-Linux.h>
#include <eacp/Core/Threads/Timer.h>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace MakeASound::Hosting
{

namespace
{
using namespace Steinberg;

// The handlers are held, so one the plugin releases while registered stays
// valid; each callback holds its own reference, so unregistering from inside
// it releases nothing the call still needs.
class EacpRunLoop
    : public FObject
    , public VST3RunLoop
    , public Linux::IRunLoop
{
public:
    ~EacpRunLoop() override { clear(); }

    bool answer(const TUID _iid, void** obj) noexcept override
    {
        if (!FUnknownPrivate::iidEqual(_iid, Linux::IRunLoop::iid))
            return false;

        addRef();
        *obj = static_cast<Linux::IRunLoop*>(this);
        return true;
    }

    tresult PLUGIN_API registerEventHandler(Linux::IEventHandler* handler,
                                            Linux::FileDescriptor fd) override
    {
        if (handler == nullptr || fd < 0)
            return kInvalidArgument;

        auto held = IPtr<Linux::IEventHandler>(handler);
        auto onReadable = [held, fd]
        {
            auto keep = held;
            keep->onFDIsSet(fd);
        };

        auto watch = Watch {held, nullptr};
        watch.watcher = EA::makeOwned<eacp::Threads::FdWatcher>(fd, onReadable);

        watches.push_back(std::move(watch));
        return kResultTrue;
    }

    tresult PLUGIN_API unregisterEventHandler(Linux::IEventHandler* handler) override
    {
        return takeAll(watches, handler).empty() ? kResultFalse : kResultTrue;
    }

    tresult PLUGIN_API registerTimer(Linux::ITimerHandler* handler,
                                     Linux::TimerInterval milliseconds) override
    {
        if (handler == nullptr)
            return kInvalidArgument;

        auto held = IPtr<Linux::ITimerHandler>(handler);
        auto interval = static_cast<std::int64_t>(
            std::clamp<Linux::TimerInterval>(milliseconds, 1, INT32_MAX));

        auto tick = Tick {held, nullptr};
        tick.timer = EA::makeOwned<eacp::Threads::Timer>(
            [held]
            {
                auto keep = held;
                keep->onTimer();
            },
            eacp::Time::MS {interval});

        ticks.push_back(std::move(tick));
        return kResultTrue;
    }

    tresult PLUGIN_API unregisterTimer(Linux::ITimerHandler* handler) override
    {
        return takeAll(ticks, handler).empty() ? kResultFalse : kResultTrue;
    }

    void clear() noexcept override
    {
        auto droppedWatches = std::move(watches);
        auto droppedTicks = std::move(ticks);
        watches.clear();
        ticks.clear();
    }

    // clang-format off
    OBJ_METHODS(EacpRunLoop, FObject)
    REFCOUNT_METHODS(FObject)
    DEFINE_INTERFACES
        DEF_INTERFACE(Linux::IRunLoop)
    END_DEFINE_INTERFACES(FObject)
    // clang-format on

private:
    struct Watch
    {
        IPtr<Linux::IEventHandler> handler;
        OwningPointer<eacp::Threads::FdWatcher> watcher;
    };

    struct Tick
    {
        IPtr<Linux::ITimerHandler> handler;
        OwningPointer<eacp::Threads::Timer> timer;
    };

    // Taken out before they are destroyed, so a handler whose release calls
    // back in finds the lists already consistent.
    template <typename Entry, typename Handler>
    static std::vector<Entry> takeAll(std::vector<Entry>& entries, Handler* handler)
    {
        auto taken = std::vector<Entry>();
        auto kept = std::vector<Entry>();

        for (auto& entry: entries)
        {
            if (entry.handler.get() == handler)
                taken.push_back(std::move(entry));
            else
                kept.push_back(std::move(entry));
        }

        entries = std::move(kept);
        return taken;
    }

    std::vector<Watch> watches;
    std::vector<Tick> ticks;
};
} // namespace

IPtr<VST3RunLoop> VST3RunLoop::create()
{
    return owned(static_cast<VST3RunLoop*>(new EacpRunLoop()));
}

} // namespace MakeASound::Hosting
