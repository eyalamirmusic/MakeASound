#include "StateContext.h"

#include <string_view>

namespace MakeASound
{
namespace
{
constexpr auto sessionTag = std::string_view {"Session"};
}

Miro::CustomOptions customOptionsFor(StateContext context)
{
    auto custom = Miro::CustomOptions {};

    if (context == StateContext::Session)
        custom.tag = std::string {sessionTag};

    return custom;
}

StateContext stateContextOf(const Miro::Reflector& ref)
{
    if (ref.customOptions().tag == sessionTag)
        return StateContext::Session;

    return StateContext::Preset;
}

bool isSession(const Miro::Reflector& ref)
{
    return stateContextOf(ref) == StateContext::Session;
}

} // namespace MakeASound
