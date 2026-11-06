#pragma once

#include "MessageThread.h"
#include "../../Common/Common.h"

#include <atomic>
#include <utility>

namespace MakeASound
{

// Hands an object to the audio thread, which never allocates or frees: it takes
// `pending` per block and retires its old one for the message thread's sweep. A
// full `retired` slot delays a publish a block. Destroy once audio has stopped.
template <typename T>
class RealtimeSwap final : Reclaimable
{
public:
    RealtimeSwap() { startReclaiming(*this); }

    ~RealtimeSwap()
    {
        stopReclaiming(*this);

        destroy(pending.exchange(nullptr));
        destroy(retired.exchange(nullptr));
        destroy(current);
    }

    RealtimeSwap(const RealtimeSwap&) = delete;
    RealtimeSwap& operator=(const RealtimeSwap&) = delete;

    // Builds a T from `args`. One the audio thread never took is freed here.
    template <typename... Args>
    void publish(Args&&... args)
    {
        auto next = EA::makeOwned<T>(std::forward<Args>(args)...);
        destroy(pending.exchange(next.release(), std::memory_order_acq_rel));
    }

    // Audio thread, once per block: the latest object, valid until the next call.
    T* currentForBlock() noexcept
    {
        if (retired.load(std::memory_order_acquire) != nullptr)
            return current;

        if (auto* next = pending.exchange(nullptr, std::memory_order_acq_rel))
        {
            retired.store(current, std::memory_order_release);
            current = next;
        }

        return current;
    }

private:
    static void destroy(T* object) { auto owned = OwningPointer<T>(object); }

    ErasedOwner takeRetired() noexcept override
    {
        return {retired.exchange(nullptr, std::memory_order_acq_rel),
                [](void* object) { destroy(static_cast<T*>(object)); }};
    }

    std::atomic<T*> pending {nullptr};
    std::atomic<T*> retired {nullptr};
    T* current = nullptr;
};

} // namespace MakeASound
