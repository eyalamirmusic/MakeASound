// The Steinberg::Linux::IRunLoop a Linux host lends its plugins, over eacp's
// message loop: descriptors, timers, unregistering from inside a callback and
// handlers still registered when the loop goes, and the host context of a load
// answering it.

#include <MakeASound/Plugin/VST3/VST3Common.h>
#include <MakeASound/Hosting/MakeASoundHosting.h>
#include <MakeASound/Hosting/VST3/VST3HostedPlugin.h>
#include <MakeASound/Hosting/VST3/VST3RunLoop.h>

#include <base/source/fobject.h>

#include <eacp/Core/Threads/EventLoop.h>

#include <NanoTest/NanoTest.h>

#include <filesystem>
#include <functional>

#include <unistd.h>

namespace HostedVST3RunLoopTests
{
using namespace nano;
using namespace MakeASound;
using namespace MakeASound::Hosting;
using namespace Steinberg;

namespace
{
// Counts its calls, reads what made its descriptor readable, and runs `then`
// after each call.
class Handler
    : public FObject
    , public Linux::IEventHandler
    , public Linux::ITimerHandler
{
public:
    void PLUGIN_API onFDIsSet(Linux::FileDescriptor fd) override
    {
        ++fdCalls;
        lastFd = fd;

        if (drain)
        {
            auto byte = char {};
            [[maybe_unused]] auto got = ::read(fd, &byte, 1);
        }

        if (then)
            then();
    }

    void PLUGIN_API onTimer() override
    {
        ++timerCalls;

        if (then)
            then();
    }

    // clang-format off
    OBJ_METHODS(Handler, FObject)
    REFCOUNT_METHODS(FObject)
    DEFINE_INTERFACES
        DEF_INTERFACE(Linux::IEventHandler)
        DEF_INTERFACE(Linux::ITimerHandler)
    END_DEFINE_INTERFACES(FObject)
    // clang-format on

    int fdCalls = 0;
    int timerCalls = 0;
    int lastFd = -1;
    bool drain = true;
    std::function<void()> then;
};

struct Pipe
{
    Pipe()
    {
        if (::pipe(fds) != 0)
            fds[0] = fds[1] = -1;
    }

    ~Pipe()
    {
        for (auto fd: fds)
            if (fd >= 0)
                ::close(fd);
    }

    Pipe(const Pipe&) = delete;
    Pipe& operator=(const Pipe&) = delete;

    void signal() const
    {
        auto byte = char {1};
        [[maybe_unused]] auto put = ::write(fds[1], &byte, 1);
    }

    int readEnd() const { return fds[0]; }

    int fds[2] = {-1, -1};
};

// The interface a plugin is handed, through the loop's own answer().
IPtr<Linux::IRunLoop> interfaceOf(VST3RunLoop& loop)
{
    auto* obj = static_cast<void*>(nullptr);

    if (!loop.answer(Linux::IRunLoop_iid, &obj))
        return {};

    return owned(static_cast<Linux::IRunLoop*>(obj));
}

IPtr<Linux::IRunLoop> makeRunLoop()
{
    auto owner = VST3RunLoop::create();
    return owner ? interfaceOf(*owner) : IPtr<Linux::IRunLoop>();
}

template <typename Predicate>
bool pumpUntil(Predicate ready)
{
    return eacp::Threads::runEventLoopUntil(ready, eacp::Time::MS {2000});
}

void pumpFor(int milliseconds)
{
    eacp::Threads::runEventLoopUntil([] { return false; },
                                     eacp::Time::MS {milliseconds});
}

auto tDescriptor = test("HostedVST3RunLoop/aReadableDescriptorCallsItsHandler") = []
{
    auto loop = makeRunLoop();
    auto handler = owned(new Handler());
    auto pipe = Pipe();
    check(loop && pipe.readEnd() >= 0);

    check(loop->registerEventHandler(handler, pipe.readEnd()) == kResultTrue);
    pipe.signal();
    check(pumpUntil([&] { return handler->fdCalls == 1; }));
    check(handler->lastFd == pipe.readEnd());

    check(loop->unregisterEventHandler(handler) == kResultTrue);
    check(loop->unregisterEventHandler(handler) == kResultFalse);

    pipe.signal();
    pumpFor(100);
    check(handler->fdCalls == 1);
};

auto tTimer = test("HostedVST3RunLoop/aTimerTicksUntilUnregistered") = []
{
    auto loop = makeRunLoop();
    auto handler = owned(new Handler());

    check(loop->registerTimer(handler, 10) == kResultTrue);
    check(pumpUntil([&] { return handler->timerCalls >= 3; }));

    check(loop->unregisterTimer(handler) == kResultTrue);
    const auto ticked = handler->timerCalls;
    pumpFor(100);
    check(handler->timerCalls == ticked);
};

auto tInside = test("HostedVST3RunLoop/aHandlerUnregistersFromItsOwnCallback") = []
{
    auto loop = makeRunLoop();
    auto pipe = Pipe();

    // Released by the test before it fires: the loop's reference keeps it.
    auto* handler = new Handler();
    handler->drain = false;
    handler->then = [&]
    {
        loop->unregisterEventHandler(handler);
        loop->unregisterTimer(handler);
    };

    loop->registerEventHandler(handler, pipe.readEnd());
    loop->registerTimer(handler, 10);
    handler->release();

    pipe.signal();
    pumpFor(150);

    auto keep = owned(new Handler());
    check(loop->registerEventHandler(keep, pipe.readEnd()) == kResultTrue);
    check(pumpUntil([&] { return keep->fdCalls >= 1; }));
};

auto tTeardown = test("HostedVST3RunLoop/handlersLeftRegisteredAreDropped") = []
{
    auto handler = owned(new Handler());
    auto pipe = Pipe();
    const auto before = handler->getRefCount();

    {
        auto loop = makeRunLoop();
        loop->registerEventHandler(handler, pipe.readEnd());
        loop->registerTimer(handler, 10);
        check(handler->getRefCount() > before);
    }

    check(handler->getRefCount() == before);

    pipe.signal();
    pumpFor(100);
    check(handler->fdCalls == 0 && handler->timerCalls == 0);

    auto cleared = VST3RunLoop::create();
    interfaceOf(*cleared)->registerEventHandler(handler, pipe.readEnd());
    cleared->clear();
    check(handler->getRefCount() == before);
    pumpFor(50);
    check(handler->fdCalls == 0);
};

struct DefaultFormats : FormatManager
{
    DefaultFormats() { addDefaultFormats(); }
};

auto tContext = test("HostedVST3RunLoop/aLoadsHostContextAnswersTheRunLoop") = []
{
    const auto bundle = std::filesystem::path(MAKEASOUND_HOST_FIXTURE_VST3_DIR)
                        / "HostFixtureEffect.vst3";
    const auto formats = DefaultFormats();
    auto scan = formats.scanTarget(bundle.string()).scan;
    check(!scan.classes.empty());

    if (scan.classes.empty())
        return;

    auto loaded = formats.load(scan.classes[0]);
    auto* vst3 = dynamic_cast<VST3HostedPlugin*>(loaded.plugin.get());
    check(vst3 != nullptr && vst3->hostContext() != nullptr);

    if (vst3 == nullptr || vst3->hostContext() == nullptr)
        return;

    check(FUnknownPtr<Linux::IRunLoop>(vst3->hostContext()).getInterface()
          != nullptr);
};
} // namespace
} // namespace HostedVST3RunLoopTests
