#pragma once

namespace MakeASound
{

// How the plugin reports its own edits to the host, on the message thread. A
// gesture is begin, performs, end; while open the wrapper drops host writes as
// echoes, and perform never writes the plugin, so set the Parameter first.
class HostEditListener
{
public:
    virtual ~HostEditListener() = default;

    virtual void beginParameterEdit(int index) noexcept = 0;
    virtual void performParameterEdit(int index, float normalized) noexcept = 0;
    virtual void endParameterEdit(int index) noexcept = 0;

    // Several gestures the host should stamp as one edit, e.g. a control driving
    // a group. Only VST3 has it and no host must honour it. Does not nest.
    virtual void beginParameterEditGroup() noexcept {}
    virtual void endParameterEditGroup() noexcept {}

    // latencySamples() moved for a reason no parameter edit carries.
    virtual void latencyChanged() noexcept {}

    // A parameter's name, range or text changed: the host should re-read them.
    virtual void parameterInfoChanged() noexcept {}
};

} // namespace MakeASound
