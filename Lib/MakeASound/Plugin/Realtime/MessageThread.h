#pragma once

#include <functional>
#include <memory>

namespace MakeASound
{

bool isMessageThread();

// Safe from any thread but the audio one: queuing the call allocates.
void callOnMessageThread(std::function<void()> function);

// Marks the calling thread as the message thread when this code lives in a
// dynamic library a foreign host loaded; a no-op in an executable. Idempotent.
// Host's UI thread only.
void adoptHostMessageThread();

// The descriptor a host's run loop watches for the message loop and one
// non-blocking pass of that loop, in MessageThread-<platform>.cpp: Linux has
// both, everywhere else the descriptor is -1 and the pass a no-op.
int messageLoopFd();
void pumpMessageLoop();

// An object of any type, owned, destroyed wherever the owner lets go of it.
using ErasedOwner = std::unique_ptr<void, void (*)(void*)>;

// Something the audio thread retires objects into for the message thread to free.
class Reclaimable
{
public:
    // Message thread, under the reclaimer's lock: hand over what is retired, if any.
    virtual ErasedOwner takeRetired() noexcept = 0;

protected:
    ~Reclaimable() = default;
};

// While registered, a message-thread sweep collects from it a few times a second.
// Register and unregister off the audio thread; unregistering waits out a sweep.
void startReclaiming(Reclaimable& reclaimable);
void stopReclaiming(Reclaimable& reclaimable);

// One sweep now, on the message thread. Returns how many objects it destroyed.
int reclaimNow();

} // namespace MakeASound
