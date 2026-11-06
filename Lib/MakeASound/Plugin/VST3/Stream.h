#pragma once

/** @file Reads and writes plugin state through a VST3 IBStream. */

#include "VST3Common.h"
#include "../State/StateContext.h"

#include "pluginterfaces/base/ibstream.h"

#include <string>
#include <string_view>

namespace MakeASound::VST3
{

/// Until a read returns no bytes, or fewer than asked with a failing status (a
/// read that delivered counts whatever its status, as Wavelab answers one), in
/// 4 KiB reads, ISizeableStream's size only reserving, since Cubase 9 reports a
/// wrong one. Empty for a stream past 2^31 - 1 bytes, as JUCE refuses one.
std::string readAll(IBStream& stream);

/// False as soon as a write fails or writes nothing.
bool writeAll(IBStream& stream, std::string_view data);

/// Session, unless the stream's IStreamAttributes names a state type other than
/// a project.
StateContext stateContextOf(IBStream* stream);

} // namespace MakeASound::VST3
