#include "FrameReading.h"
#include "ByteOrder.h"
#include "ConversionRules.h"

#include <algorithm>
#include <bit>
#include <type_traits>

namespace MakeASound::Files::Detail
{
namespace FrameDecoding
{
constexpr int sampleBytes(Encoding encoding) noexcept
{
    switch (encoding)
    {
        case Encoding::unsigned8:
        case Encoding::signed8:
            return 1;
        case Encoding::int16LE:
        case Encoding::int16BE:
            return 2;
        case Encoding::int24LE:
        case Encoding::int24BE:
            return 3;
        case Encoding::int32LE:
        case Encoding::int32BE:
        case Encoding::float32LE:
        case Encoding::float32BE:
            return 4;
        case Encoding::float64LE:
        case Encoding::float64BE:
            return 8;
    }

    return 1;
}

constexpr bool isInteger(Encoding encoding) noexcept
{
    return encoding != Encoding::float32LE && encoding != Encoding::float32BE
           && encoding != Encoding::float64LE && encoding != Encoding::float64BE;
}

// An integer sample left-justified in an int32: the stored bits become the top
// bits of a uint32, which reads back as the signed full-scale value.
template <Encoding encoding>
std::int32_t fullScaleAt(const std::uint8_t* in) noexcept
{
    auto word = std::uint32_t {};

    if constexpr (encoding == Encoding::unsigned8)
        word = (std::uint32_t) (in[0] ^ 0x80u) << 24;
    else if constexpr (encoding == Encoding::signed8)
        word = (std::uint32_t) in[0] << 24;
    else if constexpr (encoding == Encoding::int16LE)
        word = ((std::uint32_t) in[0] << 16) | ((std::uint32_t) in[1] << 24);
    else if constexpr (encoding == Encoding::int16BE)
        word = ((std::uint32_t) in[1] << 16) | ((std::uint32_t) in[0] << 24);
    else if constexpr (encoding == Encoding::int24LE)
        word = ((std::uint32_t) in[0] << 8) | ((std::uint32_t) in[1] << 16)
               | ((std::uint32_t) in[2] << 24);
    else if constexpr (encoding == Encoding::int24BE)
        word = ((std::uint32_t) in[2] << 8) | ((std::uint32_t) in[1] << 16)
               | ((std::uint32_t) in[0] << 24);
    else if constexpr (encoding == Encoding::int32LE)
        word = loadU32LE(in);
    else
        word = loadU32BE(in);

    return (std::int32_t) word;
}

template <Encoding encoding, typename Sample>
Sample decodeAt(const std::uint8_t* in) noexcept
{
    constexpr auto toFloat = std::is_same_v<Sample, float>;

    if constexpr (isInteger(encoding))
    {
        auto value = fullScaleAt<encoding>(in);

        if constexpr (toFloat)
            return fromFullScale(value);
        else
            return value;
    }
    else if constexpr (encoding == Encoding::float32LE
                       || encoding == Encoding::float32BE)
    {
        auto bits = encoding == Encoding::float32LE ? loadU32LE(in) : loadU32BE(in);
        auto value = std::bit_cast<float>(bits);

        if constexpr (toFloat)
            return value;
        else
            return toFullScale((double) value);
    }
    else
    {
        auto bits = encoding == Encoding::float64LE ? loadU64LE(in) : loadU64BE(in);
        auto value = std::bit_cast<double>(bits);

        if constexpr (toFloat)
            return (float) value;
        else
            return toFullScale(value);
    }
}

template <Encoding encoding, typename Sample>
void decodeFrames(const std::uint8_t* bytes,
                  int numFrames,
                  int numChannels,
                  Sample* const* dest,
                  int destOffset) noexcept
{
    constexpr auto width = sampleBytes(encoding);
    auto stride = numChannels * width;

    for (auto channel = 0; channel < numChannels; ++channel)
    {
        auto* out = dest[channel];

        if (out == nullptr)
            continue;

        out += destOffset;
        auto* in = bytes + channel * width;

        for (auto frame = 0; frame < numFrames; ++frame, in += stride)
            out[frame] = decodeAt<encoding, Sample>(in);
    }
}

template <typename Sample>
void decode(Encoding encoding,
            const std::uint8_t* bytes,
            int numFrames,
            int numChannels,
            Sample* const* dest,
            int destOffset) noexcept
{
    switch (encoding)
    {
        case Encoding::unsigned8:
            return decodeFrames<Encoding::unsigned8>(
                bytes, numFrames, numChannels, dest, destOffset);
        case Encoding::signed8:
            return decodeFrames<Encoding::signed8>(
                bytes, numFrames, numChannels, dest, destOffset);
        case Encoding::int16LE:
            return decodeFrames<Encoding::int16LE>(
                bytes, numFrames, numChannels, dest, destOffset);
        case Encoding::int16BE:
            return decodeFrames<Encoding::int16BE>(
                bytes, numFrames, numChannels, dest, destOffset);
        case Encoding::int24LE:
            return decodeFrames<Encoding::int24LE>(
                bytes, numFrames, numChannels, dest, destOffset);
        case Encoding::int24BE:
            return decodeFrames<Encoding::int24BE>(
                bytes, numFrames, numChannels, dest, destOffset);
        case Encoding::int32LE:
            return decodeFrames<Encoding::int32LE>(
                bytes, numFrames, numChannels, dest, destOffset);
        case Encoding::int32BE:
            return decodeFrames<Encoding::int32BE>(
                bytes, numFrames, numChannels, dest, destOffset);
        case Encoding::float32LE:
            return decodeFrames<Encoding::float32LE>(
                bytes, numFrames, numChannels, dest, destOffset);
        case Encoding::float32BE:
            return decodeFrames<Encoding::float32BE>(
                bytes, numFrames, numChannels, dest, destOffset);
        case Encoding::float64LE:
            return decodeFrames<Encoding::float64LE>(
                bytes, numFrames, numChannels, dest, destOffset);
        case Encoding::float64BE:
            return decodeFrames<Encoding::float64BE>(
                bytes, numFrames, numChannels, dest, destOffset);
    }
}

template <typename Sample>
void clear(int numChannels, Sample* const* dest, int from, int to) noexcept
{
    if (from >= to)
        return;

    for (auto channel = 0; channel < numChannels; ++channel)
        if (auto* out = dest[channel])
            std::fill(out + from, out + to, Sample {});
}

template <typename Sample>
void read(const FrameLayout& layout,
          Span<std::uint8_t> scratch,
          std::int64_t startFrame,
          int numFrames,
          Sample* const* dest) noexcept
{
    auto channels = layout.numChannels;

    // [0, before) precedes frame 0; [before, before + inside) is in the file.
    auto before = 0;

    if (startFrame < 0)
        before =
            startFrame <= -(std::int64_t) numFrames ? numFrames : (int) -startFrame;

    auto first = startFrame + before;
    auto inside = 0;

    if (before < numFrames && first < layout.numFrames)
        inside = (int) std::min<std::int64_t>(numFrames - before,
                                              layout.numFrames - first);

    clear(channels, dest, 0, before);
    clear(channels, dest, before + inside, numFrames);

    auto frameBytes = channels * sampleBytes(layout.encoding);
    auto chunkFrames = (int) (scratch.getSize() / (std::size_t) frameBytes);

    for (auto done = 0; done < inside;)
    {
        auto count = std::min(chunkFrames, inside - done);
        auto offset = layout.dataOffset + (first + done) * frameBytes;
        auto got = layout.source->read(
            offset, Span<std::uint8_t>(scratch.data(), count * frameBytes));
        auto framesGot = got / frameBytes;

        decode(layout.encoding,
               scratch.data(),
               framesGot,
               channels,
               dest,
               before + done);

        // A source that shrank since open(): what it no longer holds reads as 0.
        if (framesGot < count)
        {
            clear(channels, dest, before + done + framesGot, before + inside);
            return;
        }

        done += count;
    }
}
} // namespace FrameDecoding

std::vector<std::uint8_t>
    readChunkBytes(ByteSource& source, std::int64_t offset, std::int64_t count)
{
    // Grown a block at a time as the bytes arrive, so a size field that promises
    // more than the source holds costs only what is really there.
    constexpr auto blockBytes = std::int64_t {1} << 16;
    auto bytes = std::vector<std::uint8_t>();

    while ((std::int64_t) bytes.size() < count)
    {
        auto done = (std::int64_t) bytes.size();
        auto block = std::min(blockBytes, count - done);
        bytes.resize((std::size_t) (done + block));

        auto got = source.read(offset + done,
                               Span<std::uint8_t>(bytes.data() + done, block));
        bytes.resize((std::size_t) (done + std::max(got, 0)));

        if (got < block)
            break;
    }

    return bytes;
}

std::int64_t walkLimit(std::uint64_t formSize, std::int64_t sourceSize) noexcept
{
    if (formSize == 0 || formSize >= (std::uint64_t) sourceSize - 8)
        return sourceSize;

    return 8 + (std::int64_t) formSize;
}

int bytesPerSample(Encoding encoding) noexcept
{
    return FrameDecoding::sampleBytes(encoding);
}

std::vector<std::uint8_t> makeReadScratch(int numChannels, Encoding encoding)
{
    constexpr auto targetBytes = 1 << 16;
    auto frameBytes = numChannels * bytesPerSample(encoding);
    auto frames = std::max(1, targetBytes / frameBytes);
    return std::vector<std::uint8_t>((std::size_t) (frames * frameBytes));
}

void readFrames(const FrameLayout& layout,
                Span<std::uint8_t> scratch,
                std::int64_t startFrame,
                int numFramesToRead,
                float* const* dest) noexcept
{
    FrameDecoding::read(layout, scratch, startFrame, numFramesToRead, dest);
}

void readFrames(const FrameLayout& layout,
                Span<std::uint8_t> scratch,
                std::int64_t startFrame,
                int numFramesToRead,
                std::int32_t* const* dest) noexcept
{
    FrameDecoding::read(layout, scratch, startFrame, numFramesToRead, dest);
}
} // namespace MakeASound::Files::Detail
