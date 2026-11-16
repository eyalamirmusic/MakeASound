// clap.posix-fd-support, which a hosted CLAP plugin is offered on Linux only:
// descriptors a stub plugin registers through the host, watched on eacp's
// message loop, with on_fd called on the main thread when one is readable; a
// change of flags, the registrations the host refuses (no READ, a descriptor
// already watched, off the main thread), and a watch removed from inside its
// own callback.

#include <MakeASound/Hosting/MakeASoundHosting.h>
#include <MakeASound/Hosting/CLAP/CLAPHostedPlugin.h>

#include <eacp/Core/Threads/EventLoop.h>

#include <NanoTest/NanoTest.h>

#include <cstring>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

namespace HostedCLAPLinuxTests
{
using namespace nano;
using namespace MakeASound;
using namespace MakeASound::Hosting;

namespace
{
// A CLAP plugin with nothing but clap.posix-fd-support: every on_fd is kept,
// checked to be on the main thread, and followed by `then`.
struct FdStub
{
    FdStub()
    {
        clap.desc = &descriptor;
        clap.plugin_data = this;
        clap.init = [](const clap_plugin*) { return true; };
        clap.destroy = [](const clap_plugin* p) { of(p).destroyed = true; };
        clap.activate = [](const clap_plugin*, double, uint32_t, uint32_t)
        { return true; };
        clap.deactivate = [](const clap_plugin*) {};
        clap.start_processing = [](const clap_plugin*) { return true; };
        clap.stop_processing = [](const clap_plugin*) {};
        clap.reset = [](const clap_plugin*) {};
        clap.process = [](const clap_plugin*, const clap_process*)
        { return clap_process_status {CLAP_PROCESS_CONTINUE}; };
        clap.get_extension = [](const clap_plugin* p, const char* id) -> const void*
        {
            return std::strcmp(id, CLAP_EXT_POSIX_FD_SUPPORT) == 0 ? &of(p).fds
                                                                   : nullptr;
        };
        clap.on_main_thread = [](const clap_plugin*) {};

        fds.on_fd = [](const clap_plugin* p, int fd, clap_posix_fd_flags_t flags)
        { of(p).onFd(fd, flags); };
    }

    static FdStub& of(const clap_plugin* p)
    {
        return *static_cast<FdStub*>(p->plugin_data);
    }

    const clap_host_posix_fd_support* support() const
    {
        return static_cast<const clap_host_posix_fd_support*>(
            host->get_extension(host, CLAP_EXT_POSIX_FD_SUPPORT));
    }

    bool onMain() const
    {
        const auto* threads = static_cast<const clap_host_thread_check*>(
            host->get_extension(host, CLAP_EXT_THREAD_CHECK));
        return threads != nullptr && threads->is_main_thread(host);
    }

    void onFd(int fd, clap_posix_fd_flags_t flags)
    {
        if (destroyed || !onMain())
            ++violations;

        calls.push_back({fd, flags});

        if (drain)
        {
            auto byte = char {};
            [[maybe_unused]] auto got = ::read(fd, &byte, 1);
        }

        if (then)
            then(fd);
    }

    struct Call
    {
        int fd = -1;
        clap_posix_fd_flags_t flags = 0;
    };

    clap_plugin_descriptor descriptor {CLAP_VERSION,
                                       "com.makeasound.tests.fdstub",
                                       "FdStub",
                                       "MakeASound Tests",
                                       "",
                                       "",
                                       "",
                                       "1.0.0",
                                       "",
                                       nullptr};
    clap_plugin clap {};
    clap_plugin_posix_fd_support fds {};
    const clap_host* host = nullptr;

    std::vector<Call> calls;
    std::function<void(int fd)> then;
    bool drain = true;
    bool destroyed = false;
    int violations = 0;
};

OwningPointer<HostedPlugin> host(FdStub& stub)
{
    auto description = Hosting::PluginDescription();
    description.name = "FdStub";
    description.classId = stub.descriptor.id;

    auto result = CLAPHostedPlugin::create(description,
                                           nullptr,
                                           [&stub](const clap_host* clapHost)
                                           {
                                               stub.host = clapHost;
                                               return &stub.clap;
                                           },
                                           {.name = "Fd Host"});

    check(result.error.empty());
    check(result.plugin != nullptr);
    return std::move(result.plugin);
}

// Both ends of a pipe, closed when it goes.
struct Pipe
{
    Pipe() { check(::pipe(ends) == 0); }

    ~Pipe()
    {
        ::close(ends[0]);
        ::close(ends[1]);
    }

    int readEnd() const { return ends[0]; }

    void poke() const
    {
        auto byte = char {1};
        check(::write(ends[1], &byte, 1) == 1);
    }

    int ends[2] {-1, -1};
};

bool pumpUntil(const std::function<bool()>& ready)
{
    return eacp::Threads::runEventLoopUntil(ready, eacp::Time::MS {3000});
}

auto tDelivery = test("HostedCLAPLinux/aReadableDescriptorReachesOnFd") = []
{
    auto stub = FdStub();
    auto pipe = Pipe();

    {
        auto plugin = host(stub);
        const auto* support = stub.support();
        check(support != nullptr);

        if (support == nullptr)
            return;

        check(support->register_fd(stub.host, pipe.readEnd(), CLAP_POSIX_FD_READ));

        // Nothing to read, nothing told.
        eacp::Threads::runEventLoopFor(eacp::Time::MS {50});
        check(stub.calls.empty());

        pipe.poke();
        check(pumpUntil([&stub] { return !stub.calls.empty(); }));
        check(stub.calls.size() == 1);

        if (!stub.calls.empty())
        {
            check(stub.calls[0].fd == pipe.readEnd());
            check(stub.calls[0].flags == CLAP_POSIX_FD_READ);
        }

        // Drained by the plugin, so told once per byte.
        pipe.poke();
        check(pumpUntil([&stub] { return stub.calls.size() >= 2; }));

        check(support->unregister_fd(stub.host, pipe.readEnd()));
        check(!support->unregister_fd(stub.host, pipe.readEnd()));

        pipe.poke();
        eacp::Threads::runEventLoopFor(eacp::Time::MS {50});
        check(stub.calls.size() == 2);
    }

    check(stub.violations == 0);
};

auto tModify = test("HostedCLAPLinux/aWatchsFlagsChangeInPlace") = []
{
    auto stub = FdStub();
    auto pipe = Pipe();
    auto plugin = host(stub);
    const auto* support = stub.support();

    if (support == nullptr)
        return;

    // Only a watched descriptor can change.
    check(!support->modify_fd(stub.host, pipe.readEnd(), CLAP_POSIX_FD_READ));

    check(support->register_fd(stub.host, pipe.readEnd(), CLAP_POSIX_FD_READ));
    check(support->modify_fd(
        stub.host, pipe.readEnd(), CLAP_POSIX_FD_READ | CLAP_POSIX_FD_ERROR));

    pipe.poke();
    check(pumpUntil([&stub] { return !stub.calls.empty(); }));

    if (!stub.calls.empty())
        check(stub.calls[0].flags == CLAP_POSIX_FD_READ);

    check(support->unregister_fd(stub.host, pipe.readEnd()));
    check(stub.violations == 0);
};

auto tRefused =
    test("HostedCLAPLinux/registrationsTheHostCannotServeAreRefused") = []
{
    auto stub = FdStub();
    auto pipe = Pipe();
    auto plugin = host(stub);
    const auto* support = stub.support();

    if (support == nullptr)
        return;

    // eacp's loop tells readable and nothing else.
    check(!support->register_fd(stub.host, pipe.readEnd(), 0));
    check(!support->register_fd(stub.host, pipe.readEnd(), CLAP_POSIX_FD_WRITE));
    check(!support->register_fd(stub.host, -1, CLAP_POSIX_FD_READ));

    // One watch per descriptor.
    check(support->register_fd(stub.host, pipe.readEnd(), CLAP_POSIX_FD_READ));
    check(!support->register_fd(stub.host, pipe.readEnd(), CLAP_POSIX_FD_READ));

    // [main-thread] only.
    auto fromAnother = std::vector<bool>();
    auto thread = std::thread(
        [&]
        {
            fromAnother.push_back(
                support->modify_fd(stub.host, pipe.readEnd(), CLAP_POSIX_FD_READ));
            fromAnother.push_back(support->unregister_fd(stub.host, pipe.readEnd()));
            auto other = Pipe();
            fromAnother.push_back(support->register_fd(
                stub.host, other.readEnd(), CLAP_POSIX_FD_READ));
        });
    thread.join();
    check(fromAnother == std::vector<bool> {false, false, false});

    // Still watched after all that.
    pipe.poke();
    check(pumpUntil([&stub] { return !stub.calls.empty(); }));
    check(stub.violations == 0);
};

auto tRemoveInside =
    test("HostedCLAPLinux/aWatchRemovedFromItsOwnCallbackStops") = []
{
    auto stub = FdStub();
    auto pipe = Pipe();
    auto plugin = host(stub);
    const auto* support = stub.support();

    if (support == nullptr)
        return;

    check(support->register_fd(stub.host, pipe.readEnd(), CLAP_POSIX_FD_READ));

    // Left readable, so only the removal stops the calls.
    auto removed = false;
    stub.drain = false;
    stub.then = [&](int fd) { removed = support->unregister_fd(stub.host, fd); };

    pipe.poke();
    check(pumpUntil([&stub] { return !stub.calls.empty(); }));
    check(removed);

    eacp::Threads::runEventLoopFor(eacp::Time::MS {50});
    check(stub.calls.size() == 1);

    // And it can be watched again.
    stub.then = nullptr;
    stub.drain = true;
    check(support->register_fd(stub.host, pipe.readEnd(), CLAP_POSIX_FD_READ));
    check(pumpUntil([&stub] { return stub.calls.size() >= 2; }));
    check(stub.violations == 0);
};

auto tLeftRegistered =
    test("HostedCLAPLinux/aWatchLeftRegisteredEndsWithThePlugin") = []
{
    auto stub = FdStub();
    auto pipe = Pipe();

    {
        auto plugin = host(stub);

        if (const auto* support = stub.support())
            check(
                support->register_fd(stub.host, pipe.readEnd(), CLAP_POSIX_FD_READ));
    }

    pipe.poke();
    eacp::Threads::runEventLoopFor(eacp::Time::MS {50});
    check(stub.destroyed);
    check(stub.calls.empty());
    check(stub.violations == 0);
};
} // namespace
} // namespace HostedCLAPLinuxTests
