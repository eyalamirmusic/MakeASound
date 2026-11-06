#pragma once

/** @file Channel and ConstChannel, views over one channel's samples. */

#include "../Common/Common.h"

namespace MakeASound
{

/// Non-owning views over a single channel's samples; what a Buffer hands out.
using Channel = Span<float>;
using ConstChannel = Span<const float>;

} // namespace MakeASound
