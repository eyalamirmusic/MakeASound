#include "Plugin.h"

#include <algorithm>

namespace MakeASound
{

bool Plugin::acceptsLayout(const BusLayout& proposed) const
{
    return getBusLayout().acceptsExactly(proposed);
}

void Plugin::processBypassed(ProcessContext& context) noexcept
{
    bypassDelay.process(
        context.mainInput(), context.mainOutput(), std::max(0, latencySamples()));

    for (auto bus = 1; bus < context.outputs.size(); ++bus)
        context.outputs[bus].clear();

    auto midiBuses = std::min(context.midiIn.size(), context.midiOut.size());

    for (auto bus = 0; bus < midiBuses; ++bus)
    {
        auto& out = context.midiOut[bus];

        for (const auto& event: context.midiIn[bus])
            if (out.size() < out.capacity())
                out.add(event);
    }
}

void Plugin::setParameters(ParameterList list)
{
    parameterList = std::move(list);

    if (hostBypass != nullptr)
        parameterList.addHostBypass(*hostBypass);

    if (parameterIdentity)
        parameterList.setIdentity(*parameterIdentity);
}

void Plugin::setParameterIdentity(ParameterIdentity identity)
{
    parameterIdentity = identity;
    parameterList.setIdentity(identity);
}

void Plugin::addHostBypass()
{
    if (hostBypass == nullptr)
        hostBypass = EA::makeOwned<BoolParam>(
            "Bypass",
            false,
            ParameterOptions {
                .id = "__bypass", .bypass = true, .sessionOnly = true});

    parameterList.addHostBypass(*hostBypass);
}

BoolParam* Plugin::activeHostBypass() const noexcept
{
    auto index = parameterList.bypassIndex();

    if (hostBypass == nullptr || index < 0
        || &parameterList[index] != hostBypass.get())
        return nullptr;

    return hostBypass.get();
}

std::string Plugin::saveState(StateContext)
{
    return {};
}

void Plugin::loadState(std::string_view, StateContext)
{
}

std::string Plugin::saveStateWithoutMessageThread(StateContext)
{
    return {};
}

void Plugin::loadParameters(std::string_view, StateContext)
{
}

bool Plugin::loadForeignState(const ForeignState&)
{
    return false;
}

void Plugin::onTrackInfoChanged(const TrackInfo&)
{
}

void Plugin::setTrackInfo(TrackInfo info)
{
    currentTrackInfo = std::move(info);
    onTrackInfoChanged(currentTrackInfo);
}

void Plugin::loadStateExceptParameters(std::string_view data, StateContext context)
{
    loadState(data, context);
}

void Plugin::notifyHostLatencyChanged() noexcept
{
    // Message thread, so the bypass's delay can grow here rather than wait for
    // the adapter's next latency check.
    fitBypassDelay();

    if (editListener != nullptr)
        editListener->latencyChanged();
}

void Plugin::fitBypassDelay() noexcept
{
    if (hostBypass == nullptr)
        return;

    // A failed allocation leaves the delay clamping to what it holds.
    try
    {
        bypassDelay.ensureDelay(latencySamples());
    }
    catch (...)
    {
    }
}

void Plugin::notifyHostParameterInfoChanged() noexcept
{
    if (editListener != nullptr)
        editListener->parameterInfoChanged();
}

void Plugin::notifyHostParameterValuesChanged() noexcept
{
    if (editListener != nullptr)
        editListener->parameterValuesChanged();
}

void Plugin::notifyHostStateChanged() noexcept
{
    if (editListener != nullptr)
        editListener->stateChanged();
}

} // namespace MakeASound
