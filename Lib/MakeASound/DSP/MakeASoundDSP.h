#pragma once

/** @file
    The optional DSP library: processors built on the core vocabulary alone
    (MakeASoundCore.h), with no device code. TestSynth is a monophonic
    instrument for checking that a stream or a plugin wrapper makes sound.
    Everything here runs inside Processor::process on the audio thread and
    allocates only in prepare().
*/

#include "../MakeASoundCore.h"
#include "TestSynth.h"
