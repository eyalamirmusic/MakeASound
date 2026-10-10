#pragma once

#include <Miro/Miro.h>

namespace MakeASound
{

// Why a document is moving. A DAW session carries everything; a preset leaves
// out what belongs to the session alone (session-only parameters, file paths).
// Rides through a reflection walk as Miro's CustomOptions tag.
enum class StateContext
{
    Preset,
    Session
};

Miro::CustomOptions customOptionsFor(StateContext context);
StateContext stateContextOf(const Miro::Reflector& ref);
bool isSession(const Miro::Reflector& ref);

} // namespace MakeASound
