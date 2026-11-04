#pragma once

#include "FourCC.h"

#include <array>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace MakeASound::Files
{
// The metadata chunks of a WAV file, each as a plain struct holding every field
// the chunk stores. Texts are UTF-8 bytes as stored, with no terminating zero.
// WavWriter.h lists how each is laid out in a file.

// 'bext', the Broadcast Wave description. The fixed text fields are stored
// zero-padded to their width (in brackets); a longer text is cut at that width.
struct Bwav
{
    std::string description; // [256]
    std::string originator; // [32]
    std::string originatorReference; // [32]
    std::string originationDate; // [10] "yyyy-mm-dd"
    std::string originationTime; // [8] "hh:mm:ss"
    std::uint64_t timeReference = 0; // samples since midnight
    std::uint16_t version = 0;
    std::array<std::uint8_t, 64> umid {};
    std::array<std::uint8_t, 190>
        reserved {}; // version 2's loudness fields live here
    std::string codingHistory;

    bool operator==(const Bwav&) const = default;
};

struct SampleLoop
{
    std::uint32_t identifier = 0;
    std::uint32_t type = 0;
    std::uint32_t start = 0;
    std::uint32_t end = 0;
    std::uint32_t fraction = 0;
    std::uint32_t playCount = 0;

    bool operator==(const SampleLoop&) const = default;
};

// 'smpl', the sampler chunk: tuning, SMPTE offset and loops. The loop count is
// loops.size(). samplerDataSize is the stored sampler-data size field, written as
// given; extraBytes are any bytes after the loops, written and read verbatim.
struct Smpl
{
    std::uint32_t manufacturer = 0;
    std::uint32_t product = 0;
    std::uint32_t samplePeriod = 0;
    std::uint32_t midiUnityNote = 60;
    std::uint32_t midiPitchFraction = 0;
    std::uint32_t smpteFormat = 0;
    std::uint32_t smpteOffset = 0;
    std::uint32_t samplerDataSize = 0;
    std::vector<SampleLoop> loops;
    std::vector<std::uint8_t> extraBytes;

    bool operator==(const Smpl&) const = default;
};

// 'inst', the instrument chunk: seven bytes.
struct Inst
{
    std::uint8_t baseNote = 60;
    std::int8_t detune = 0; // cents
    std::int8_t gain = 0; // dB
    std::uint8_t lowNote = 0;
    std::uint8_t highNote = 127;
    std::uint8_t lowVelocity = 1;
    std::uint8_t highVelocity = 127;

    bool operator==(const Inst&) const = default;
};

struct CuePoint
{
    std::uint32_t identifier = 0;
    std::uint32_t position = 0; // play order
    FourCC chunkId = "data";
    std::uint32_t chunkStart = 0;
    std::uint32_t blockStart = 0;
    std::uint32_t sampleOffset = 0;

    bool operator==(const CuePoint&) const = default;
};

// 'cue ', the cue points.
struct Cue
{
    std::vector<CuePoint> points;

    bool operator==(const Cue&) const = default;
};

// A 'labl' or 'note' entry: text attached to a cue point.
struct CueLabel
{
    std::uint32_t cuePointId = 0;
    std::string text;

    bool operator==(const CueLabel&) const = default;
};

// An 'ltxt' entry: a region of samples starting at a cue point, with text.
struct CueRegion
{
    std::uint32_t cuePointId = 0;
    std::uint32_t sampleLength = 0;
    FourCC purpose = "rgn ";
    std::uint16_t country = 0;
    std::uint16_t language = 0;
    std::uint16_t dialect = 0;
    std::uint16_t codePage = 0;
    std::string text;

    bool operator==(const CueRegion&) const = default;
};

// A 'LIST' chunk of type 'adtl': the cue points' labels, notes and regions,
// stored in that order (labels first, then notes, then regions).
struct AssociatedData
{
    std::vector<CueLabel> labels;
    std::vector<CueLabel> notes;
    std::vector<CueRegion> regions;

    bool operator==(const AssociatedData&) const = default;
};

// One entry of a LIST/INFO chunk: a four-character key ('IART', 'ICMT', ...) and
// its text.
struct InfoEntry
{
    FourCC id;
    std::string value;

    bool operator==(const InfoEntry&) const = default;
};

// A 'LIST' chunk of type 'INFO', its entries in file order.
struct ListInfo
{
    std::vector<InfoEntry> entries;

    bool operator==(const ListInfo&) const = default;
};

// 'acid', the loop description some editors write: 24 bytes.
struct Acid
{
    std::uint32_t flags = 0; // 0x01 one shot, 0x02 root set, 0x04 stretch,
    // 0x08 disk based, 0x10 the fifth flag
    std::uint16_t rootNote = 0;
    std::uint16_t reserved1 = 0;
    float reserved2 = 0.0f;
    std::uint32_t numBeats = 0;
    std::uint16_t meterDenominator = 0;
    std::uint16_t meterNumerator = 0;
    float tempo = 0.0f;

    bool operator==(const Acid&) const = default;
};

// 'Trkn', a text chunk some editors write their loop information into.
struct Trkn
{
    std::string text;

    bool operator==(const Trkn&) const = default;
};

// 'axml', an XML document (EBU Core and the like).
struct Axml
{
    std::string xml;

    bool operator==(const Axml&) const = default;
};

// Any other chunk, its id and its bytes as stored (for a LIST of another type,
// the bytes start with that type).
struct RawChunk
{
    FourCC id;
    std::vector<std::uint8_t> bytes;

    bool operator==(const RawChunk&) const = default;
};

using Chunk = std::variant<Bwav,
                           Smpl,
                           Inst,
                           Cue,
                           AssociatedData,
                           ListInfo,
                           Acid,
                           Trkn,
                           Axml,
                           RawChunk>;

// A file's metadata: its chunks in file order. A writer writes them in this
// order; a reader lists them in the order it found them.
struct Metadata
{
    std::vector<Chunk> chunks;

    bool empty() const noexcept { return chunks.empty(); }

    // The first chunk of type T, or null.
    template <typename T>
    const T* find() const noexcept
    {
        for (auto& chunk: chunks)
            if (auto* found = std::get_if<T>(&chunk))
                return found;

        return nullptr;
    }

    template <typename T>
    T* find() noexcept
    {
        for (auto& chunk: chunks)
            if (auto* found = std::get_if<T>(&chunk))
                return found;

        return nullptr;
    }

    bool operator==(const Metadata&) const = default;
};
} // namespace MakeASound::Files
