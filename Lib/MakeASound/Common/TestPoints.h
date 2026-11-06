#pragma once

#include <atomic>

/** @file
    Named points in library code where a test can hold a thread, to force an
    interleaving that timing alone would rarely give: a listener thread stopped
    in the middle of a callback while another thread tears its object down. Only
    a test installs a handler. Without one, a point is a single atomic load of a
    null pointer, and none sits on an audio-thread path.
*/
namespace MakeASound::TestPoints
{
/// Called on the thread that reached the point, with the point's name and the
/// object it was reached for. It may block that thread.
using Handler = void (*)(const char* name, const void* object);

/// Null unless a test installed one.
inline std::atomic<Handler> handler {nullptr};

/// Calls the installed handler, if any, on the calling thread.
inline void reach(const char* name, const void* object) noexcept
{
    if (auto* installed = handler.load(std::memory_order_acquire))
        installed(name, object);
}
} // namespace MakeASound::TestPoints
