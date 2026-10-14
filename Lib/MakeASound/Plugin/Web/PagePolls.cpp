#include "PagePolls.h"

#include <eacp/Core/Core.h>

namespace MakeASound::Web
{

PagePolls::PagePolls() = default;

PagePolls::~PagePolls()
{
    stop();
}

void PagePolls::add(std::function<void()> poll, int hz)
{
    auto& added =
        polls.add({.call = std::move(poll), .hz = hz > 0 ? hz : defaultHz});

    if (running)
    {
        added.call();
        startTimer(added);
    }
}

void PagePolls::start()
{
    if (running)
        return;

    running = true;

    for (const auto& poll: polls)
    {
        poll.call();
        startTimer(poll);
    }
}

void PagePolls::stop()
{
    running = false;
    timers.clear();
}

void PagePolls::pollAll()
{
    for (const auto& poll: polls)
        poll.call();
}

void PagePolls::startTimer(const Poll& poll)
{
    // The call is copied: the polls vector may grow under a live timer.
    timers.add(EA::makeOwned<eacp::Threads::Timer>(poll.call, poll.hz));
}

} // namespace MakeASound::Web
