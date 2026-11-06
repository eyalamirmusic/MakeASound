#pragma once

/** @file
    The VST3 SDK headers every VST3 adapter source includes first. First in every
    source file of the VST3 target: this header renames IEditController's
    setState/getState while it includes ivsteditcontroller.h, and any earlier
    include of that header defeats the rename.
*/

#include "public.sdk/source/vst/vstsinglecomponenteffect.h"

namespace MakeASound::VST3
{
namespace Vst = Steinberg::Vst;

using Steinberg::FIDString;
using Steinberg::FUnknown;
using Steinberg::IBStream;
using Steinberg::int32;
using Steinberg::TBool;
using Steinberg::tresult;
using Steinberg::uint32;
} // namespace MakeASound::VST3
