#include "Plugin.h"

namespace MakeASound
{

bool Plugin::acceptsLayout(const BusLayout& proposed) const
{ return getBusLayout().acceptsExactly(proposed); }

std::string Plugin::saveState(StateContext)
{ return {}; }

void Plugin::loadState(std::string_view, StateContext)
{
}

std::string Plugin::saveStateWithoutMessageThread(StateContext)
{ return {}; }

void Plugin::loadParameters(std::string_view, StateContext)
{
}

void Plugin::loadStateExceptParameters(std::string_view data, StateContext context)
{ loadState(data, context); }

void Plugin::notifyHostLatencyChanged() noexcept
{
    if (editListener != nullptr)
        editListener->latencyChanged();
}

void Plugin::notifyHostParameterInfoChanged() noexcept
{
    if (editListener != nullptr)
        editListener->parameterInfoChanged();
}

} // namespace MakeASound
