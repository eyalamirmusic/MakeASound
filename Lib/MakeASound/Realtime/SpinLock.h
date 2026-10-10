#pragma once

#include "../Common/Common.h"

namespace MakeASound
{

using ScopedSpinLock = EA::Locks::ScopedSpinLock<EA::Locks::PrimitiveSpinLock>;

// Takes the lock only if it was free. The audio thread skips a drain rather than
// waiting behind the open or close that is the only thing ever holding it.
struct ScopedTryLock
{
    explicit ScopedTryLock(EA::Locks::PrimitiveSpinLock& lockToUse)
        : lock(lockToUse)
        , held(lockToUse.tryLock())
    {
    }

    ~ScopedTryLock()
    {
        if (held)
            lock.unlock();
    }

    ScopedTryLock(const ScopedTryLock&) = delete;
    ScopedTryLock& operator=(const ScopedTryLock&) = delete;

    EA::Locks::PrimitiveSpinLock& lock;
    bool held {};
};

} // namespace MakeASound
