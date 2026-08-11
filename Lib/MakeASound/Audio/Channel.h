#pragma once

#include "../Common/Common.h"

namespace MakeASound
{

// A non-owning view over a single channel's samples.
using Channel = Span<float>;

} // namespace MakeASound
