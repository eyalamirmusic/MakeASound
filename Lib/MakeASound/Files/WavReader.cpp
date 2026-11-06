#include "WavReader.h"
#include "ByteOrder.h"
#include "FrameReading.h"
#include "WavFormat.h"

#include <algorithm>
#include <bit>
#include <cstring>
#include <utility>

namespace MakeASound::Files
{
namespace WavParsing
{
using Detail::PresentBytes;

// Limits on what a file can make open() hold, whatever its chunk sizes say: the
// bytes read of a format chunk (the extensible form's 40 are all this reader
// uses), the bytes of one metadata chunk (a larger one is skipped), the number of
// metadata chunks kept, and the entries kept from one LIST.
constexpr auto formatBytesUsed = std::int64_t {40};
constexpr auto maxMetadataChunkBytes = std::int64_t {16} << 20;
constexpr auto maxMetadataChunks = 4096;
constexpr auto maxListEntries = 4096;

FourCC fourCCAt(const std::uint8_t* bytes) noexcept
{
    return FourCC::fromLittleEndian(Detail::loadU32LE(bytes));
}

struct Format
{
    int channels = 0;
    double rate = 0.0;
    int bits = 0;
    bool isFloat = false;
    std::uint32_t mask = 0;
};

bool isIntegerDepth(int bits) noexcept
{
    return bits == 8 || bits == 16 || bits == 24 || bits == 32;
}

bool isFloatDepth(int bits) noexcept
{
    return bits == 32 || bits == 64;
}

// A format chunk this reader decodes (section 5.2 of the behaviour spec), or false.
bool parseFormat(PresentBytes payload, Format& format) noexcept
{
    if (payload.size < 16)
        return false;

    auto tag = payload.u16(0);
    format.channels = payload.u16(2);
    format.rate = (double) payload.u32(4);
    format.bits = payload.u16(14);

    if (format.channels < 1 || format.rate < 1.0)
        return false;

    if (tag == Detail::wavTagExtensible)
    {
        if (payload.size < 40)
            return false;

        auto tail = payload.bytes + 28;
        auto known = std::memcmp(tail, Detail::standardSubFormatTail, 12) == 0
                     || std::memcmp(tail, Detail::ambisonicSubFormatTail, 12) == 0;

        auto subFormat = payload.u32(24);

        if (!known || subFormat > 0xffff)
            return false;

        tag = (std::uint16_t) subFormat;
        format.mask = payload.u32(20);
    }

    if (tag == Detail::wavTagPcm && isIntegerDepth(format.bits))
        return true;

    if (tag == Detail::wavTagFloat && isFloatDepth(format.bits))
    {
        format.isFloat = true;
        return true;
    }

    return false;
}

Bwav parseBext(PresentBytes payload)
{
    auto bwav = Bwav {};
    bwav.description = payload.text(0, 256);
    bwav.originator = payload.text(256, 32);
    bwav.originatorReference = payload.text(288, 32);
    bwav.originationDate = payload.text(320, 10);
    bwav.originationTime = payload.text(330, 8);
    bwav.timeReference = payload.u64(338);
    bwav.version = payload.u16(346);

    for (auto i = 0; i < (int) bwav.umid.size(); ++i)
        bwav.umid[(std::size_t) i] = payload.at(348 + i);

    for (auto i = 0; i < (int) bwav.reserved.size(); ++i)
        bwav.reserved[(std::size_t) i] = payload.at(412 + i);

    bwav.codingHistory = payload.text(602, payload.size);
    return bwav;
}

Smpl parseSmpl(PresentBytes payload)
{
    constexpr auto fixedBytes = 36;
    constexpr auto loopBytes = 24;

    auto smpl = Smpl {};
    smpl.manufacturer = payload.u32(0);
    smpl.product = payload.u32(4);
    smpl.samplePeriod = payload.u32(8);
    smpl.midiUnityNote = payload.u32(12);
    smpl.midiPitchFraction = payload.u32(16);
    smpl.smpteFormat = payload.u32(20);
    smpl.smpteOffset = payload.u32(24);
    smpl.samplerDataSize = payload.u32(32);

    auto room =
        payload.size > fixedBytes ? (payload.size - fixedBytes) / loopBytes : 0;
    auto count = std::min<std::int64_t>(payload.u32(28), room);

    for (auto i = std::int64_t {0}; i < count; ++i)
    {
        auto at = fixedBytes + loopBytes * i;
        smpl.loops.push_back({payload.u32(at),
                              payload.u32(at + 4),
                              payload.u32(at + 8),
                              payload.u32(at + 12),
                              payload.u32(at + 16),
                              payload.u32(at + 20)});
    }

    auto extraStart = fixedBytes + loopBytes * count;

    if (payload.size > extraStart)
        smpl.extraBytes.assign(payload.bytes + extraStart,
                               payload.bytes + payload.size);

    return smpl;
}

Inst parseInst(PresentBytes payload) noexcept
{
    return {payload.at(0),
            (std::int8_t) payload.at(1),
            (std::int8_t) payload.at(2),
            payload.at(3),
            payload.at(4),
            payload.at(5),
            payload.at(6)};
}

Cue parseCue(PresentBytes payload)
{
    constexpr auto pointBytes = 24;

    auto cue = Cue {};
    auto room = payload.size > 4 ? (payload.size - 4) / pointBytes : 0;
    auto count = std::min<std::int64_t>(payload.u32(0), room);

    for (auto i = std::int64_t {0}; i < count; ++i)
    {
        auto at = 4 + pointBytes * i;
        auto point = CuePoint {};
        point.identifier = payload.u32(at);
        point.position = payload.u32(at + 4);
        point.chunkId = FourCC::fromLittleEndian(payload.u32(at + 8));
        point.chunkStart = payload.u32(at + 12);
        point.blockStart = payload.u32(at + 16);
        point.sampleOffset = payload.u32(at + 20);
        cue.points.push_back(point);
    }

    return cue;
}

// Calls visit(id, entryPayload) for each sub-chunk of a LIST after its type, until
// the visitor has kept maxListEntries of them (it returns whether it kept one), so
// a list of many empty entries cannot cost a struct each without limit.
template <typename Visitor>
void forEachListEntry(PresentBytes payload, Visitor&& visit)
{
    auto kept = 0;

    for (auto at = std::int64_t {4};
         at + 8 <= payload.size && kept < maxListEntries;)
    {
        auto id = FourCC::fromLittleEndian(payload.u32(at));
        auto size = (std::int64_t) payload.u32(at + 4);

        if (visit(id, payload.range(at + 8, size)))
            ++kept;

        at += 8 + size + (size & 1);
    }
}

CueLabel parseCueLabel(PresentBytes entry)
{
    return {entry.u32(0), entry.text(4, entry.size)};
}

CueRegion parseCueRegion(PresentBytes entry)
{
    auto region = CueRegion {};
    region.cuePointId = entry.u32(0);
    region.sampleLength = entry.u32(4);
    region.purpose = FourCC::fromLittleEndian(entry.u32(8));
    region.country = entry.u16(12);
    region.language = entry.u16(14);
    region.dialect = entry.u16(16);
    region.codePage = entry.u16(18);
    region.text = entry.text(20, entry.size);
    return region;
}

Chunk parseList(PresentBytes payload)
{
    auto type =
        payload.size < 4 ? FourCC() : FourCC::fromLittleEndian(payload.u32(0));

    if (type == FourCC("INFO") || type == FourCC("info"))
    {
        auto info = ListInfo {};
        forEachListEntry(payload,
                         [&](FourCC id, PresentBytes entry)
                         {
                             info.entries.push_back({id, entry.text(0, entry.size)});
                             return true;
                         });
        return info;
    }

    if (type == FourCC("adtl"))
    {
        auto data = AssociatedData {};
        forEachListEntry(payload,
                         [&](FourCC id, PresentBytes entry)
                         {
                             if (id == FourCC("labl"))
                                 data.labels.push_back(parseCueLabel(entry));
                             else if (id == FourCC("note"))
                                 data.notes.push_back(parseCueLabel(entry));
                             else if (id == FourCC("ltxt"))
                                 data.regions.push_back(parseCueRegion(entry));
                             else
                                 return false;

                             return true;
                         });
        return data;
    }

    // Too short for a type, or a type this reader does not parse: the bytes as
    // they are, type included.
    return RawChunk {"LIST", {payload.bytes, payload.bytes + payload.size}};
}

Acid parseAcid(PresentBytes payload) noexcept
{
    auto acid = Acid {};
    acid.flags = payload.u32(0);
    acid.rootNote = payload.u16(4);
    acid.unknown1 = payload.u16(6);
    acid.unknown2 = std::bit_cast<float>(payload.u32(8));
    acid.numBeats = payload.u32(12);
    acid.meterDenominator = payload.u16(16);
    acid.meterNumerator = payload.u16(18);
    acid.tempo = std::bit_cast<float>(payload.u32(20));
    return acid;
}

// Chunks that carry nothing for the caller: header space and sizes.
bool isSkipped(FourCC id) noexcept
{
    return id == FourCC("ds64") || id == FourCC("JUNK") || id == FourCC("junk")
           || id == FourCC("PAD ") || id == FourCC("fact");
}

// A metadata chunk as its struct, or a RawChunk for an id this reader does not
// parse.
Chunk parseChunk(FourCC id, const std::vector<std::uint8_t>& bytes)
{
    auto payload = PresentBytes {bytes.data(), (std::int64_t) bytes.size()};

    if (id == FourCC("bext"))
        return parseBext(payload);

    if (id == FourCC("smpl"))
        return parseSmpl(payload);

    if (id == FourCC("inst") || id == FourCC("INST"))
        return parseInst(payload);

    if (id == FourCC("cue "))
        return parseCue(payload);

    if (id == FourCC("LIST"))
        return parseList(payload);

    if (id == FourCC("acid"))
        return parseAcid(payload);

    if (id == FourCC("Trkn"))
        return Trkn {payload.text(0, payload.size)};

    if (id == FourCC("axml"))
        return Axml {payload.text(0, payload.size)};

    return RawChunk {id, bytes};
}

Detail::Encoding encodingFor(int bits, bool isFloat) noexcept
{
    using Detail::Encoding;

    switch (bits)
    {
        case 8:
            return Encoding::unsigned8;
        case 16:
            return Encoding::int16LE;
        case 24:
            return Encoding::int24LE;
        case 32:
            return isFloat ? Encoding::float32LE : Encoding::int32LE;
        default:
            return Encoding::float64LE;
    }
}
} // namespace WavParsing

void WavReader::forget() noexcept
{
    source = nullptr;
    channels = 0;
    rate = 0.0;
    bits = 0;
    floatingPoint = false;
    rf64 = false;
    mask = 0;
    frames = 0;
    dataOffset = 0;
    chunks.chunks.clear();
    scratch.clear();
}

bool WavReader::open(ByteSource& newSource)
{
    using namespace WavParsing;

    forget();

    auto total = newSource.size();
    std::uint8_t header[12];

    if (total < 12 || newSource.read(0, header) != 12)
        return false;

    auto form = fourCCAt(header);
    auto isRF64 = form == FourCC("RF64");

    if ((!isRF64 && form != FourCC("RIFF"))
        || fourCCAt(header + 8) != FourCC("WAVE"))
        return false;

    auto limit = total;
    auto ds64DataSize = std::uint64_t {0};

    if (isRF64)
    {
        // The ds64 chunk must come first, with its 28 bytes of sizes present.
        std::uint8_t ds64[36];

        if (newSource.read(12, ds64) != 36 || fourCCAt(ds64) != FourCC("ds64")
            || Detail::loadU32LE(ds64 + 4) < 28)
            return false;

        limit = Detail::walkLimit(Detail::loadU64LE(ds64 + 8), total);
        ds64DataSize = Detail::loadU64LE(ds64 + 16);
    }
    else
    {
        auto riffSize = Detail::loadU32LE(header + 4);

        if (riffSize != Detail::sizeUnknown)
            limit = Detail::walkLimit(riffSize, total);
    }

    auto format = Format {};
    auto formatSeen = false;
    auto formatAccepted = false;
    auto dataSeen = false;
    auto dataStart = std::int64_t {0};
    auto dataPresent = std::int64_t {0};
    auto metadata = Metadata {};

    for (auto pos = std::int64_t {12}; pos + 8 <= limit;)
    {
        std::uint8_t chunkHeader[8];

        if (newSource.read(pos, chunkHeader) != 8)
            break;

        auto id = fourCCAt(chunkHeader);
        auto size = (std::uint64_t) Detail::loadU32LE(chunkHeader + 4);
        auto payload = pos + 8;
        auto room = (std::uint64_t) (limit - payload);
        auto available = (std::int64_t) std::min(size, room);

        if (id == FourCC("data"))
        {
            if (!dataSeen)
            {
                dataSeen = true;
                dataStart = payload;

                // Data is measured against the whole source, not the walk limit,
                // so a file whose RIFF size is wrong still reads its samples.
                auto there = (std::uint64_t) (total - payload);

                if (!isRF64 && size == Detail::sizeUnknown)
                {
                    dataPresent = (std::int64_t) there;
                    break;
                }

                if (isRF64 && size == Detail::sizeUnknown)
                    size = ds64DataSize;

                dataPresent = (std::int64_t) std::min(size, there);
            }
        }
        else if (id == FourCC("fmt "))
        {
            if (!formatSeen)
            {
                formatSeen = true;
                auto bytes = Detail::readChunkBytes(
                    newSource, payload, std::min(available, formatBytesUsed));
                formatAccepted =
                    parseFormat({bytes.data(), (std::int64_t) bytes.size()}, format);
            }
        }
        else if (!isSkipped(id) && available <= maxMetadataChunkBytes
                 && (int) metadata.chunks.size() < maxMetadataChunks)
        {
            auto bytes = Detail::readChunkBytes(newSource, payload, available);
            metadata.chunks.push_back(parseChunk(id, bytes));
        }

        // A chunk that runs to or past the limit is the last one.
        if (size >= room)
            break;

        pos = payload + (std::int64_t) size + (std::int64_t) (size & 1);
    }

    if (!formatSeen || !formatAccepted)
        return false;

    auto encoding = encodingFor(format.bits, format.isFloat);
    auto frameBytes = format.channels * Detail::bytesPerSample(encoding);

    source = &newSource;
    channels = format.channels;
    rate = format.rate;
    bits = format.bits;
    floatingPoint = format.isFloat;
    rf64 = isRF64;
    mask = format.mask;
    frames = dataSeen ? dataPresent / frameBytes : 0;
    dataOffset = dataStart;
    chunks = std::move(metadata);
    scratch = Detail::makeReadScratch(channels, encoding);
    return true;
}

bool WavReader::isOpen() const noexcept
{
    return source != nullptr;
}

int WavReader::numChannels() const noexcept
{
    return channels;
}

double WavReader::sampleRate() const noexcept
{
    return rate;
}

int WavReader::bitDepth() const noexcept
{
    return bits;
}

bool WavReader::isFloat() const noexcept
{
    return floatingPoint;
}

std::int64_t WavReader::numFrames() const noexcept
{
    return frames;
}

bool WavReader::isRF64() const noexcept
{
    return rf64;
}

std::uint32_t WavReader::channelMask() const noexcept
{
    return mask;
}

const Metadata& WavReader::metadata() const noexcept
{
    return chunks;
}

bool WavReader::read(std::int64_t startFrame,
                     int numFramesToRead,
                     float* const* dest) noexcept
{
    if (source == nullptr || numFramesToRead < 0 || dest == nullptr)
        return false;

    auto layout = Detail::FrameLayout {source,
                                       dataOffset,
                                       frames,
                                       channels,
                                       WavParsing::encodingFor(bits, floatingPoint)};
    Detail::readFrames(layout, scratch, startFrame, numFramesToRead, dest);
    return true;
}

bool WavReader::readInt32(std::int64_t startFrame,
                          int numFramesToRead,
                          std::int32_t* const* dest) noexcept
{
    if (source == nullptr || numFramesToRead < 0 || dest == nullptr)
        return false;

    auto layout = Detail::FrameLayout {source,
                                       dataOffset,
                                       frames,
                                       channels,
                                       WavParsing::encodingFor(bits, floatingPoint)};
    Detail::readFrames(layout, scratch, startFrame, numFramesToRead, dest);
    return true;
}
} // namespace MakeASound::Files
