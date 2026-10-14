#pragma once

#include "../../Common/Common.h"

#include <functional>

namespace eacp::Threads
{
class Timer;
}

namespace MakeASound::Web
{

// The periodic calls a WebEditor makes while its page is attached, each on a
// timer at its own rate: ParameterApi::poll(), and any API's of the product's
// own (WebEditor::use, addPoll). Message thread.
class PagePolls
{
public:
    // The rate of a poll added with none, as the parameters are polled.
    static constexpr auto defaultHz = 30;

    PagePolls();
    ~PagePolls();

    PagePolls(const PagePolls&) = delete;
    PagePolls& operator=(const PagePolls&) = delete;

    // `hz` of 0 or less is defaultHz. Added while running, it starts at once.
    void add(std::function<void()> poll, int hz = 0);

    // Calls each poll once now, then on its timer until stop(). A no-op when
    // already running.
    void start();
    void stop();

    bool isRunning() const noexcept { return running; }

    // Calls each poll once, as its timer would.
    void pollAll();

    int size() const noexcept { return polls.size(); }

private:
    struct Poll
    {
        std::function<void()> call;
        int hz = defaultHz;
    };

    void startTimer(const Poll& poll);

    Vector<Poll> polls;
    Vector<OwningPointer<eacp::Threads::Timer>> timers;
    bool running = false;
};

} // namespace MakeASound::Web
