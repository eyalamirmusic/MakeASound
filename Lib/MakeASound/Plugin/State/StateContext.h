#pragma once

/** @file Whether a document is a preset or a session, and how that rides through a reflection walk. */

#include <Miro/Miro.h>

namespace MakeASound
{

/// Why a document is moving. A DAW session carries everything; a preset leaves
/// out what belongs to the session alone (session-only parameters, file paths).
/// Rides through a reflection walk as Miro's CustomOptions tag.
enum class StateContext
{
    Preset,
    Session
};

/// The options tag that marks a reflection walk as a Session; untagged means Preset.
Miro::CustomOptions customOptionsFor(StateContext context);
/// Reads the context back from a walk's options tag.
StateContext stateContextOf(const Miro::Reflector& ref);
bool isSession(const Miro::Reflector& ref);

} // namespace MakeASound
