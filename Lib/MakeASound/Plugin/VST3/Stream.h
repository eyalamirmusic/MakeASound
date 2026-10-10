#pragma once

#include "VST3Common.h"
#include "../State/StateContext.h"

#include "pluginterfaces/base/ibstream.h"

#include <string>
#include <string_view>

namespace MakeASound::VST3
{

// Until a read returns no bytes or fails.
std::string readAll(IBStream& stream);

bool writeAll(IBStream& stream, std::string_view data);

// Session, unless the stream's IStreamAttributes names a state type other than
// a project.
StateContext stateContextOf(IBStream* stream);

} // namespace MakeASound::VST3
