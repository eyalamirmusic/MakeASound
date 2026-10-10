#include "VST3Common.h"
#include "HostRunLoop.h"
#include "../Realtime/MessageThread.h"

#include "pluginterfaces/base/funknownimpl.h"
#include "pluginterfaces/gui/iplugview.h"

#include <utility>

namespace MakeASound::VST3
{

namespace
{
// Pumps eacp's message loop whenever the host's run loop sees its descriptor
// readable. C++ owns it: the host's references are not counted.
class RunLoopPump
    : public HostRunLoop
    , public Steinberg::Linux::IEventHandler
{
public:
    explicit RunLoopPump(Steinberg::IPtr<Steinberg::Linux::IRunLoop> loopToUse)
        : loop(std::move(loopToUse))
    {
        auto fd = messageLoopFd();

        if (fd >= 0)
            registered =
                loop->registerEventHandler(this, fd) == Steinberg::kResultOk;
    }

    ~RunLoopPump() override
    {
        if (registered)
            loop->unregisterEventHandler(this);
    }

    RunLoopPump(const RunLoopPump&) = delete;
    RunLoopPump& operator=(const RunLoopPump&) = delete;

    void PLUGIN_API onFDIsSet(Steinberg::Linux::FileDescriptor) override
    {
        pumpMessageLoop();
    }

    tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override
    {
        QUERY_INTERFACE(iid, obj, FUnknown::iid, Steinberg::Linux::IEventHandler)
        QUERY_INTERFACE(iid,
                        obj,
                        Steinberg::Linux::IEventHandler::iid,
                        Steinberg::Linux::IEventHandler)

        *obj = nullptr;
        return Steinberg::kNoInterface;
    }

    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }

private:
    Steinberg::IPtr<Steinberg::Linux::IRunLoop> loop;
    bool registered = false;
};
} // namespace

OwningPointer<HostRunLoop> attachHostRunLoop(FUnknown* context)
{
    if (auto loop = Steinberg::U::cast<Steinberg::Linux::IRunLoop>(context))
        return EA::makeOwned<RunLoopPump>(loop);

    return {};
}

} // namespace MakeASound::VST3
