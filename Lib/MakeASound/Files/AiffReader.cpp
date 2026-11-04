#include "AiffReader.h"
#include "ByteOrder.h"
#include "FourCC.h"
#include "FrameReading.h"

#include <algorithm>
#include <cmath>

namespace MakeASound::Files
{
namespace AiffParsing
{
using Detail::PresentBytes;

FourCC idAt(const std::uint8_t* bytes) noexcept
{
    return FourCC::fromLittleEndian(Detail::loadU32LE(bytes));
}

// The IEEE 754 80-bit extended number of a COMM chunk's rate: sign, 15-bit
// exponent (bias 16383), 64-bit mantissa with an explicit integer bit. Rounded once
// to the nearest double (the mantissa's conversion; the scaling is exact for any
// rate). NaN for infinities and NaNs, which no rate can be.
double extendedToDouble(PresentBytes bytes, std::int64_t offset) noexcept
{
    auto signAndExponent = bytes.u16BE(offset);
    auto exponent = (int) (signAndExponent & 0x7fff);
    auto mantissa = ((std::uint64_t) bytes.u32BE(offset + 2) << 32)
                    | (std::uint64_t) bytes.u32BE(offset + 6);

    if (exponent == 0x7fff)
        return std::nan("");

    auto magnitude = std::ldexp((double) mantissa, exponent - 16383 - 63);
    return (signAndExponent & 0x8000) != 0 ? -magnitude : magnitude;
}

struct Common
{
    int channels = 0;
    std::uint32_t frames = 0;
    int sampleSize = 0;
    double rate = 0.0;
    FourCC compression = "NONE";
};

Common parseCommon(PresentBytes payload, bool isAifc) noexcept
{
    auto common = Common {};
    common.channels = (std::int16_t) payload.u16BE(0);
    common.frames = payload.u32BE(2);
    common.sampleSize = (std::int16_t) payload.u16BE(6);
    common.rate = extendedToDouble(payload, 8);

    if (isAifc && payload.size >= 22)
        common.compression = FourCC::fromLittleEndian(payload.u32(18));

    return common;
}

struct SampleFormat
{
    int bits = 0;
    bool isFloat = false;
    bool isLittleEndian = false;
};

// The samples' depth and byte order for a compression type and sample size, or
// false for a compression or size this reader does not decode.
bool chooseSampleFormat(const Common& common, SampleFormat& format) noexcept
{
    auto type = common.compression;

    if (type == FourCC("fl32") || type == FourCC("FL32"))
    {
        format = {32, true, false};
        return true;
    }

    if (type == FourCC("fl64") || type == FourCC("FL64"))
    {
        format = {64, true, false};
        return true;
    }

    auto isLittleEndian = type == FourCC("sowt");

    if (!isLittleEndian && type != FourCC("NONE") && type != FourCC("twos"))
        return false;

    auto bits = common.sampleSize;

    if (bits != 8 && bits != 16 && bits != 24 && bits != 32)
        return false;

    format = {bits, false, isLittleEndian};
    return true;
}

Detail::Encoding encodingFor(int bits, bool isFloat, bool isLittleEndian) noexcept
{
    using Detail::Encoding;

    if (isFloat)
        return bits == 64 ? Encoding::float64BE : Encoding::float32BE;

    switch (bits)
    {
        case 8:
            return Encoding::signed8;
        case 16:
            return isLittleEndian ? Encoding::int16LE : Encoding::int16BE;
        case 24:
            return isLittleEndian ? Encoding::int24LE : Encoding::int24BE;
        default:
            return isLittleEndian ? Encoding::int32LE : Encoding::int32BE;
    }
}
} // namespace AiffParsing

void AiffReader::forget() noexcept
{
    source = nullptr;
    channels = 0;
    rate = 0.0;
    bits = 0;
    floatingPoint = false;
    littleEndian = false;
    frames = 0;
    dataOffset = 0;
    scratch.clear();
}

bool AiffReader::open(ByteSource& newSource)
{
    using namespace AiffParsing;

    forget();

    auto total = newSource.size();
    std::uint8_t header[12];

    if (total < 12 || newSource.read(0, header) != 12)
        return false;

    auto formType = idAt(header + 8);
    auto isAifc = formType == FourCC("AIFC");

    if (idAt(header) != FourCC("FORM") || (!isAifc && formType != FourCC("AIFF")))
        return false;

    auto limit = Detail::walkLimit(Detail::loadU32BE(header + 4), total);
    auto common = Common {};
    auto commonSeen = false;
    auto soundSeen = false;
    auto soundStart = std::int64_t {0};
    auto soundPresent = std::int64_t {0};

    for (auto pos = std::int64_t {12}; pos + 8 <= limit;)
    {
        std::uint8_t chunkHeader[8];

        if (newSource.read(pos, chunkHeader) != 8)
            break;

        auto id = idAt(chunkHeader);
        auto size = (std::int64_t) Detail::loadU32BE(chunkHeader + 4);
        auto payload = pos + 8;
        auto room = limit - payload;

        if (id == FourCC("COMM") && !commonSeen)
        {
            commonSeen = true;
            auto bytes =
                Detail::readChunkBytes(newSource, payload, std::min(size, room));
            common =
                parseCommon({bytes.data(), (std::int64_t) bytes.size()}, isAifc);
        }
        else if (id == FourCC("SSND") && !soundSeen)
        {
            std::uint8_t offsetBytes[4] = {};
            newSource.read(payload, offsetBytes);
            auto offset = (std::int64_t) Detail::loadU32BE(offsetBytes);

            soundSeen = true;
            soundStart = payload + 8 + offset;
            soundPresent = std::max<std::int64_t>(
                0, std::min(size - 8 - offset, total - soundStart));
        }

        pos = payload + size + (size & 1);
    }

    if (!commonSeen || common.channels < 1 || !std::isfinite(common.rate)
        || !(common.rate > 0.0))
        return false;

    auto format = SampleFormat {};

    if (!chooseSampleFormat(common, format))
        return false;

    auto encoding = encodingFor(format.bits, format.isFloat, format.isLittleEndian);
    auto frameBytes = common.channels * Detail::bytesPerSample(encoding);

    source = &newSource;
    channels = common.channels;
    rate = common.rate;
    bits = format.bits;
    floatingPoint = format.isFloat;
    littleEndian = format.isLittleEndian;
    frames = soundSeen
                 ? std::min<std::int64_t>(common.frames, soundPresent / frameBytes)
                 : 0;
    dataOffset = soundStart;
    scratch = Detail::makeReadScratch(channels, encoding);
    return true;
}

bool AiffReader::isOpen() const noexcept
{
    return source != nullptr;
}

int AiffReader::numChannels() const noexcept
{
    return channels;
}

double AiffReader::sampleRate() const noexcept
{
    return rate;
}

int AiffReader::bitDepth() const noexcept
{
    return bits;
}

bool AiffReader::isFloat() const noexcept
{
    return floatingPoint;
}

std::int64_t AiffReader::numFrames() const noexcept
{
    return frames;
}

bool AiffReader::read(std::int64_t startFrame,
                      int numFramesToRead,
                      float* const* dest) noexcept
{
    if (source == nullptr || numFramesToRead < 0 || dest == nullptr)
        return false;

    auto layout = Detail::FrameLayout {
        source,
        dataOffset,
        frames,
        channels,
        AiffParsing::encodingFor(bits, floatingPoint, littleEndian)};
    Detail::readFrames(layout, scratch, startFrame, numFramesToRead, dest);
    return true;
}

bool AiffReader::readInt32(std::int64_t startFrame,
                           int numFramesToRead,
                           std::int32_t* const* dest) noexcept
{
    if (source == nullptr || numFramesToRead < 0 || dest == nullptr)
        return false;

    auto layout = Detail::FrameLayout {
        source,
        dataOffset,
        frames,
        channels,
        AiffParsing::encodingFor(bits, floatingPoint, littleEndian)};
    Detail::readFrames(layout, scratch, startFrame, numFramesToRead, dest);
    return true;
}
} // namespace MakeASound::Files
