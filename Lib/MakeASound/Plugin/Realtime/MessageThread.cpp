#include "MessageThread.h"

#include "../../Common/Common.h"

#include <eacp/Core/Platform/Platform.h>
#include <eacp/Core/Threads/EventLoop.h>
#include <eacp/Core/Threads/ThreadUtils.h>
#include <eacp/Core/Utils/Singleton.h>

#include <mutex>

namespace MakeASound
{

namespace
{
class Reclaimer
{
public:
    void add(Reclaimable& reclaimable)
    {
        {
            auto lock = std::lock_guard(mutex);
            registered.add(&reclaimable);

            if (ticking)
                return;

            ticking = true;
        }

        scheduleTick();
    }

    void remove(Reclaimable& reclaimable)
    {
        auto lock = std::lock_guard(mutex);
        registered.removeAllMatches(&reclaimable);
    }

    int sweep()
    {
        // Destroyed outside the lock: a retired object's destructor may well
        // register or unregister a swap of its own.
        auto garbage = takeRetired();
        return garbage.size();
    }

private:
    Vector<ErasedOwner> takeRetired()
    {
        auto garbage = Vector<ErasedOwner>();
        auto lock = std::lock_guard(mutex);

        for (auto* reclaimable: registered)
            if (auto retired = reclaimable->takeRetired())
                garbage.add(std::move(retired));

        return garbage;
    }

    void tick()
    {
        sweep();

        {
            auto lock = std::lock_guard(mutex);
            ticking = !registered.empty();

            if (!ticking)
                return;
        }

        scheduleTick();
    }

    static void scheduleTick()
    {
        eacp::Threads::callAfter(
            eacp::Time::MS {tickIntervalMs},
            [] { eacp::Singleton::getImmortal<Reclaimer>().tick(); });
    }

    static constexpr int tickIntervalMs = 250;

    std::mutex mutex;
    Vector<Reclaimable*> registered;
    bool ticking = false;
};

Reclaimer& getReclaimer()
{
    return eacp::Singleton::getImmortal<Reclaimer>();
}
} // namespace

bool isMessageThread()
{
    return eacp::Threads::isMainThread();
}

void callOnMessageThread(std::function<void()> function)
{
    eacp::Threads::callAsync(std::move(function));
}

void adoptHostMessageThread()
{
    if (eacp::Platform::isDLL())
        eacp::Threads::attachCurrentThreadAsMain();
}

void releaseHostMessageThread()
{
    if (eacp::Platform::isDLL())
        eacp::Threads::detachCurrentThreadAsMain();
}

void startReclaiming(Reclaimable& reclaimable)
{
    getReclaimer().add(reclaimable);
}

void stopReclaiming(Reclaimable& reclaimable)
{
    getReclaimer().remove(reclaimable);
}

int reclaimNow()
{
    return getReclaimer().sweep();
}

} // namespace MakeASound
