#include "WavWriter.h"
#include "ByteOrder.h"
#include "ConversionRules.h"
#include "WavFormat.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <type_traits>
#include <variant>

namespace MakeASound::Files
{
namespace WavLayout
{
// Appends little-endian numbers, ids and texts to a byte vector.
struct ByteBuilder
{
    ByteBuilder& u8(std::uint32_t number)
    {
        bytes.push_back((std::uint8_t) number);
        return *this;
    }

    ByteBuilder& u16(std::uint32_t number) { return u8(number).u8(number >> 8); }

    ByteBuilder& u32(std::uint32_t number) { return u16(number).u16(number >> 16); }

    ByteBuilder& u64(std::uint64_t number)
    {
        return u32((std::uint32_t) number).u32((std::uint32_t) (number >> 32));
    }

    ByteBuilder& id(FourCC code)
    {
        bytes.insert(bytes.end(), code.bytes.begin(), code.bytes.end());
        return *this;
    }

    template <typename Range>
    ByteBuilder& append(const Range& range)
    {
        bytes.insert(bytes.end(), std::begin(range), std::end(range));
        return *this;
    }

    ByteBuilder& zeros(std::size_t count)
    {
        bytes.insert(bytes.end(), count, 0);
        return *this;
    }

    // The text, a terminating zero, and one more zero when that makes the count
    // odd: the INFO/adtl text convention.
    ByteBuilder& textField(const std::string& text)
    {
        append(text);
        return zeros(textFieldSize(text) - text.size());
    }

    // A whole chunk: id, size, payload, and the pad byte for an odd payload.
    ByteBuilder& chunk(FourCC code, const std::vector<std::uint8_t>& payload)
    {
        id(code).u32((std::uint32_t) payload.size()).append(payload);
        return zeros(payload.size() & 1);
    }

    static std::size_t textFieldSize(const std::string& text)
    {
        return (text.size() + 2) & ~std::size_t {1};
    }

    std::vector<std::uint8_t> bytes;
};

// How many bytes of 'text' fit 'width' without splitting a UTF-8 sequence.
std::size_t utf8PrefixLength(const std::string& text, std::size_t width)
{
    if (text.size() <= width)
        return text.size();

    if (width == 0)
        return 0;

    auto isContinuation = [&](std::size_t index)
    { return ((std::uint8_t) text[index] & 0xc0) == 0x80; };

    // Back from the last byte kept to the start of its sequence (past at most
    // three continuation bytes), and drop that sequence if the cut splits it.
    auto start = width - 1;

    while (start > 0 && width - 1 - start < 3 && isContinuation(start))
        --start;

    auto first = (std::uint8_t) text[start];
    auto length = std::size_t {1};

    if (first >= 0xf0 && first < 0xf8)
        length = 4;
    else if (first >= 0xe0 && first < 0xf0)
        length = 3;
    else if (first >= 0xc0 && first < 0xe0)
        length = 2;

    return start + length > width ? start : width;
}

// A text in a zero-filled field of 'width' bytes at 'offset'.
void putFixedText(std::vector<std::uint8_t>& payload,
                  std::size_t offset,
                  const std::string& text,
                  std::size_t width)
{
    auto length = utf8PrefixLength(text, width);
    std::copy(text.begin(),
              text.begin() + (std::ptrdiff_t) length,
              payload.begin() + (std::ptrdiff_t) offset);
}

void appendChunk(ByteBuilder& out, const Bwav& bwav)
{
    auto length = (602 + bwav.codingHistory.size() + 1 + 3) & ~std::size_t {3};
    auto payload = std::vector<std::uint8_t>(length, 0);

    putFixedText(payload, 0, bwav.description, 256);
    putFixedText(payload, 256, bwav.originator, 32);
    putFixedText(payload, 288, bwav.originatorReference, 32);
    putFixedText(payload, 320, bwav.originationDate, 10);
    putFixedText(payload, 330, bwav.originationTime, 8);
    Detail::storeU32LE(payload.data() + 338, (std::uint32_t) bwav.timeReference);
    Detail::storeU32LE(payload.data() + 342,
                       (std::uint32_t) (bwav.timeReference >> 32));
    Detail::storeU16LE(payload.data() + 346, bwav.version);
    std::copy(bwav.umid.begin(), bwav.umid.end(), payload.begin() + 348);
    std::copy(bwav.reserved.begin(), bwav.reserved.end(), payload.begin() + 412);
    std::copy(
        bwav.codingHistory.begin(), bwav.codingHistory.end(), payload.begin() + 602);

    out.chunk("bext", payload);
}

void appendChunk(ByteBuilder& out, const Smpl& smpl)
{
    auto payload = ByteBuilder {};
    payload.u32(smpl.manufacturer)
        .u32(smpl.product)
        .u32(smpl.samplePeriod)
        .u32(smpl.midiUnityNote)
        .u32(smpl.midiPitchFraction)
        .u32(smpl.smpteFormat)
        .u32(smpl.smpteOffset)
        .u32((std::uint32_t) smpl.loops.size())
        .u32(smpl.samplerDataSize);

    for (auto& loop: smpl.loops)
        payload.u32(loop.identifier)
            .u32(loop.type)
            .u32(loop.start)
            .u32(loop.end)
            .u32(loop.fraction)
            .u32(loop.playCount);

    payload.append(smpl.extraBytes);
    out.chunk("smpl", payload.bytes);
}

void appendChunk(ByteBuilder& out, const Inst& inst)
{
    auto payload = ByteBuilder {};
    payload.u8(inst.unshiftedNote)
        .u8((std::uint8_t) inst.fineTune)
        .u8((std::uint8_t) inst.gain)
        .u8(inst.lowNote)
        .u8(inst.highNote)
        .u8(inst.lowVelocity)
        .u8(inst.highVelocity);
    out.chunk("inst", payload.bytes);
}

void appendChunk(ByteBuilder& out, const Cue& cue)
{
    auto payload = ByteBuilder {};
    payload.u32((std::uint32_t) cue.points.size());

    for (auto& point: cue.points)
        payload.u32(point.identifier)
            .u32(point.position)
            .id(point.chunkId)
            .u32(point.chunkStart)
            .u32(point.blockStart)
            .u32(point.sampleOffset);

    out.chunk("cue ", payload.bytes);
}

void appendChunk(ByteBuilder& out, const AssociatedData& data)
{
    auto payload = ByteBuilder {};
    payload.id("adtl");

    auto appendLabel = [&](FourCC id, const CueLabel& label)
    {
        payload.id(id)
            .u32((std::uint32_t) (4 + ByteBuilder::textFieldSize(label.text)))
            .u32(label.cuePointId)
            .textField(label.text);
    };

    for (auto& label: data.labels)
        appendLabel("labl", label);

    for (auto& note: data.notes)
        appendLabel("note", note);

    for (auto& region: data.regions)
        payload.id("ltxt")
            .u32((std::uint32_t) (20 + ByteBuilder::textFieldSize(region.text)))
            .u32(region.cuePointId)
            .u32(region.sampleLength)
            .id(region.purpose)
            .u16(region.country)
            .u16(region.language)
            .u16(region.dialect)
            .u16(region.codePage)
            .textField(region.text);

    out.chunk("LIST", payload.bytes);
}

void appendChunk(ByteBuilder& out, const ListInfo& info)
{
    auto payload = ByteBuilder {};
    payload.id("INFO");

    for (auto& entry: info.entries)
        payload.id(entry.id)
            .u32((std::uint32_t) ByteBuilder::textFieldSize(entry.value))
            .textField(entry.value);

    out.chunk("LIST", payload.bytes);
}

void appendChunk(ByteBuilder& out, const Acid& acid)
{
    auto payload = ByteBuilder {};
    payload.u32(acid.flags)
        .u16(acid.rootNote)
        .u16(acid.unknown1)
        .u32(std::bit_cast<std::uint32_t>(acid.unknown2))
        .u32(acid.numBeats)
        .u16(acid.meterDenominator)
        .u16(acid.meterNumerator)
        .u32(std::bit_cast<std::uint32_t>(acid.tempo));
    out.chunk("acid", payload.bytes);
}

void appendChunk(ByteBuilder& out, const Trkn& trkn)
{
    out.chunk("Trkn", ByteBuilder {}.textField(trkn.text).bytes);
}

void appendChunk(ByteBuilder& out, const Axml& axml)
{
    out.chunk("axml", ByteBuilder {}.append(axml.xml).bytes);
}

void appendChunk(ByteBuilder& out, const RawChunk& raw)
{
    out.chunk(raw.id, raw.bytes);
}

std::vector<std::uint8_t> serialise(const Metadata& metadata)
{
    auto out = ByteBuilder {};

    for (auto& chunk: metadata.chunks)
        std::visit([&](const auto& each) { appendChunk(out, each); }, chunk);

    return std::move(out.bytes);
}

int bytesPerSample(BitDepth depth) noexcept
{
    switch (depth)
    {
        case BitDepth::pcm8:
            return 1;
        case BitDepth::pcm16:
            return 2;
        case BitDepth::pcm24:
            return 3;
        case BitDepth::pcm32:
        case BitDepth::float32:
            return 4;
    }

    return 0;
}

std::uint32_t defaultMask(int numChannels) noexcept
{
    switch (numChannels)
    {
        case 3:
            return 0x7; // FL FR FC
        case 4:
            return 0x33; // FL FR BL BR
        case 5:
            return 0x37; // FL FR FC BL BR
        case 6:
            return 0x3f; // FL FR FC LFE BL BR
        case 7:
            return 0xf7; // FL FR FC BL BR FLC FRC
        case 8:
            return 0xff; // the same and LFE
        default:
            return 0;
    }
}

bool isReservedId(FourCC id) noexcept
{
    return id == FourCC("fmt ") || id == FourCC("data") || id == FourCC("ds64");
}

// One sample at the file's depth: float input through the full-scale int32, int32
// input as it is; float32 files take float input's bits unchanged.
template <BitDepth depth, typename Sample>
void store(std::uint8_t* out, Sample sample) noexcept
{
    constexpr auto isFloatInput = std::is_same_v<Sample, float>;

    if constexpr (depth == BitDepth::float32)
    {
        auto value = 0.0f;

        if constexpr (isFloatInput)
            value = sample;
        else
            value = Detail::fromFullScale(sample);

        Detail::storeU32LE(out, std::bit_cast<std::uint32_t>(value));
    }
    else
    {
        auto fullScale = std::int32_t {};

        if constexpr (isFloatInput)
            fullScale = Detail::toFullScale((double) sample);
        else
            fullScale = sample;

        // The top bytes of the full-scale value are the arithmetic shift's low ones.
        auto word = (std::uint32_t) fullScale;

        if constexpr (depth == BitDepth::pcm8)
        {
            out[0] = (std::uint8_t) ((word >> 24) ^ 0x80);
        }
        else if constexpr (depth == BitDepth::pcm16)
        {
            out[0] = (std::uint8_t) (word >> 16);
            out[1] = (std::uint8_t) (word >> 24);
        }
        else if constexpr (depth == BitDepth::pcm24)
        {
            out[0] = (std::uint8_t) (word >> 8);
            out[1] = (std::uint8_t) (word >> 16);
            out[2] = (std::uint8_t) (word >> 24);
        }
        else
        {
            Detail::storeU32LE(out, word);
        }
    }
}

template <BitDepth depth, typename Sample>
void encodeFrames(const Sample* const* channels,
                  int numChannels,
                  int firstFrame,
                  int numFrames,
                  std::uint8_t* out) noexcept
{
    constexpr auto width = depth == BitDepth::pcm8    ? 1
                           : depth == BitDepth::pcm16 ? 2
                           : depth == BitDepth::pcm24 ? 3
                                                      : 4;
    constexpr auto silence = std::uint8_t {depth == BitDepth::pcm8 ? 0x80 : 0};
    auto stride = numChannels * width;

    for (auto channel = 0; channel < numChannels; ++channel)
    {
        auto* in = channels[channel];
        auto* at = out + channel * width;

        if (in == nullptr)
        {
            for (auto frame = 0; frame < numFrames; ++frame, at += stride)
                std::fill(at, at + width, silence);

            continue;
        }

        in += firstFrame;

        for (auto frame = 0; frame < numFrames; ++frame, at += stride)
            store<depth>(at, in[frame]);
    }
}

template <typename Sample>
void encode(BitDepth depth,
            const Sample* const* channels,
            int numChannels,
            int firstFrame,
            int numFrames,
            std::uint8_t* out) noexcept
{
    switch (depth)
    {
        case BitDepth::pcm8:
            return encodeFrames<BitDepth::pcm8>(
                channels, numChannels, firstFrame, numFrames, out);
        case BitDepth::pcm16:
            return encodeFrames<BitDepth::pcm16>(
                channels, numChannels, firstFrame, numFrames, out);
        case BitDepth::pcm24:
            return encodeFrames<BitDepth::pcm24>(
                channels, numChannels, firstFrame, numFrames, out);
        case BitDepth::pcm32:
            return encodeFrames<BitDepth::pcm32>(
                channels, numChannels, firstFrame, numFrames, out);
        case BitDepth::float32:
            return encodeFrames<BitDepth::float32>(
                channels, numChannels, firstFrame, numFrames, out);
    }
}

constexpr auto maxRiffSize = std::uint64_t {0xffffffffu};
} // namespace WavLayout

WavWriter::~WavWriter()
{
    if (sink == nullptr)
        return;

    // finish() builds the final header and asks the sink to write it, either of
    // which may throw (std::bad_alloc, a consumer's sink); nothing may leave a
    // destructor. A caller that needs to know calls finish() itself.
    try
    {
        finish();
    }
    catch (...)
    {
        sink = nullptr;
    }
}

bool WavWriter::open(ByteSink& newSink, const WavWriterOptions& options)
{
    using namespace WavLayout;

    if (sink != nullptr)
        return false;

    auto rateOk = std::isfinite(options.sampleRate) && options.sampleRate >= 1.0
                  && options.sampleRate < 4294967296.0;
    auto width = bytesPerSample(options.bitDepth);

    if (!rateOk || width == 0 || options.numChannels < 1
        || (std::int64_t) options.numChannels * width > 65535)
        return false;

    for (auto& chunk: options.metadata.chunks)
        if (auto* raw = std::get_if<RawChunk>(&chunk);
            raw != nullptr && isReservedId(raw->id))
            return false;

    depth = options.bitDepth;
    numChannels = options.numChannels;
    storedRate = (std::uint32_t) options.sampleRate;
    mask = options.channelMask.value_or(defaultMask(numChannels));
    padded = options.headerPadding;
    metadataFirst = options.metadataBeforeData;
    alwaysRF64 = options.alwaysRF64;
    metadataBytes = serialise(options.metadata);
    headerStart = newSink.position();
    dataBytes = 0;
    frames = 0;
    failed = false;

    auto header = buildHeader(0, alwaysRF64);
    headerBytes = (std::int64_t) header.size();

    // The header of a finished file with no frames, so the sink holds a valid WAV.
    header = buildHeader((std::uint64_t) headerBytes - 8, alwaysRF64);

    if (!newSink.write(header))
        return false;

    auto blockAlign = numChannels * width;
    auto chunkFrames = std::max(1, (1 << 16) / blockAlign);
    scratch.assign((std::size_t) (chunkFrames * blockAlign), 0);
    sink = &newSink;
    return true;
}

std::vector<std::uint8_t> WavWriter::buildHeader(std::uint64_t riffSize,
                                                 bool asRF64) const
{
    using namespace WavLayout;

    auto width = bytesPerSample(depth);
    auto bits = (std::uint32_t) width * 8;
    auto blockAlign = (std::uint32_t) (numChannels * width);
    auto tag = depth == BitDepth::float32 ? Detail::wavTagFloat : Detail::wavTagPcm;
    auto extensible = asRF64 || mask != 0;

    auto out = ByteBuilder {};
    out.id(asRF64 ? FourCC("RF64") : FourCC("RIFF"))
        .u32(asRF64 ? Detail::sizeUnknown : (std::uint32_t) riffSize)
        .id("WAVE");

    // Padding and ds64 take the same room, so the next chunk starts at 96 either
    // way and a padded RIFF header can become an RF64 one in place.
    if (asRF64)
        out.id("ds64")
            .u32(28)
            .u64(riffSize)
            .u64((std::uint64_t) dataBytes)
            .u64(0)
            .u32(0);
    else if (padded)
        out.id("JUNK").u32(extensible ? 28 : 52).zeros(extensible ? 28 : 52);

    out.id("fmt ")
        .u32(extensible ? 40 : 16)
        .u16(extensible ? Detail::wavTagExtensible : tag)
        .u16((std::uint32_t) numChannels)
        .u32(storedRate)
        .u32((std::uint32_t) ((std::uint64_t) storedRate * blockAlign))
        .u16(blockAlign)
        .u16(bits);

    if (extensible)
        out.u16(22).u16(bits).u32(mask).u32(tag).append(
            Detail::standardSubFormatTail);

    if (metadataFirst)
        out.append(metadataBytes);

    out.id("data").u32(asRF64 ? Detail::sizeUnknown : (std::uint32_t) dataBytes);
    return std::move(out.bytes);
}

bool WavWriter::isOpen() const noexcept
{
    return sink != nullptr;
}

template <typename Sample>
bool WavWriter::appendFrames(const Sample* const* channels, int numFramesToWrite)
{
    if (sink == nullptr || failed || numFramesToWrite < 0 || channels == nullptr)
        return false;

    if (numFramesToWrite == 0)
        return true;

    auto blockAlign = numChannels * WavLayout::bytesPerSample(depth);
    auto newBytes = (std::int64_t) numFramesToWrite * blockAlign;

    // Without the padding a file cannot become RF64, so a write that would take
    // the finished RIFF size past 32 bits is refused (without failing the writer).
    if (!padded && !alwaysRF64)
    {
        auto data = dataBytes + newBytes;
        auto trailing = metadataFirst ? 0 : (std::int64_t) metadataBytes.size();
        auto riffSize = headerBytes + data + (data & 1) + trailing - 8;

        if ((std::uint64_t) riffSize > WavLayout::maxRiffSize)
            return false;
    }

    // Mono float32 on a little-endian machine: the caller's floats are the file's
    // bytes already.
    if constexpr (std::is_same_v<Sample, float>
                  && std::endian::native == std::endian::little)
    {
        if (depth == BitDepth::float32 && numChannels == 1 && channels[0] != nullptr)
        {
            auto bytes = Span<const std::uint8_t>((const std::uint8_t*) channels[0],
                                                  (std::size_t) newBytes);

            if (!sink->write(bytes))
            {
                failed = true;
                return false;
            }

            frames += numFramesToWrite;
            dataBytes += newBytes;
            return true;
        }
    }

    auto chunkFrames = (int) scratch.size() / blockAlign;

    for (auto done = 0; done < numFramesToWrite;)
    {
        auto count = std::min(chunkFrames, numFramesToWrite - done);
        WavLayout::encode(depth, channels, numChannels, done, count, scratch.data());

        if (!sink->write(
                Span<const std::uint8_t>(scratch.data(), count * blockAlign)))
        {
            failed = true;
            return false;
        }

        done += count;
        frames += count;
        dataBytes += (std::int64_t) count * blockAlign;
    }

    return true;
}

bool WavWriter::write(const float* const* channels, int numFramesToWrite)
{
    return appendFrames(channels, numFramesToWrite);
}

bool WavWriter::writeInt32(const std::int32_t* const* channels, int numFramesToWrite)
{
    return appendFrames(channels, numFramesToWrite);
}

bool WavWriter::finish()
{
    if (sink == nullptr)
        return false;

    auto ok = !failed;

    if ((dataBytes & 1) != 0)
    {
        const std::uint8_t pad[1] = {0};
        ok = sink->write(pad) && ok;
    }

    if (!metadataFirst && !metadataBytes.empty())
        ok = sink->write(metadataBytes) && ok;

    auto end = sink->position();
    auto riffSize = (std::uint64_t) (end - headerStart - 8);
    auto asRF64 = alwaysRF64 || (padded && riffSize > WavLayout::maxRiffSize);

    // Only an unpadded file could get here (its writes refuse to), and it has no
    // room for the RF64 header: its size cannot be stored.
    if (!asRF64 && riffSize > WavLayout::maxRiffSize)
        ok = false;

    auto header = buildHeader(riffSize, asRF64);
    ok = sink->seek(headerStart) && sink->write(header) && sink->seek(end) && ok;

    sink = nullptr;
    scratch.clear();
    return ok;
}

std::int64_t WavWriter::framesWritten() const noexcept
{
    return frames;
}
} // namespace MakeASound::Files
