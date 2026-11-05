#pragma once

// The optional audio-file library: WAV reading and writing, AIFF reading, over
// byte sources and sinks of the caller's choosing. Built on the core alone.

#include "../MakeASoundCore.h"
#include "ByteStreams.h"
#include "MemoryStreams.h"
#include "FileStreams.h"
#include "SampleConversion.h"
#include "FourCC.h"
#include "Metadata.h"
#include "WavReader.h"
#include "WavWriter.h"
#include "AiffReader.h"
