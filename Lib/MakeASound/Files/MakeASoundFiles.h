#pragma once

/** @file
    The optional audio-file library (MakeASoundFiles, namespace
    MakeASound::Files): WAV and RF64 reading and writing with their metadata
    chunks, and AIFF/AIFF-C reading. Built on MakeASoundCore alone, on every
    platform.

    Entry points: WavReader and AiffReader read from a ByteSource, WavWriter
    writes into a ByteSink. FileSource/FileSink and MemorySource/MemorySink are
    the stock sources and sinks; derive from ByteSource or ByteSink for any
    other stream.

    - Readers and writers never own their source or sink: it must outlive them.
    - Nothing throws on a bad file: open() returns false, and a file cut short
      reads as far as its bytes go, the rest as silence.
    - read() never allocates, so it can run on an audio thread once open.
    - WavWriter::finish() returning true only means the sink took the bytes;
      check FileSink::close() before trusting a file on disk.
*/

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
