#pragma once

// Builders shared by the Files*Tests suites: byte-by-byte file construction,
// the synthetic input the pinned WAV files were made from, and small read/write
// shortcuts. Everything is inline and lives in FilesTestHelpers, so the suites
// can share one unity batch.

#include <MakeASound/Files/MakeASoundFiles.h>

#include <NanoTest/NanoTest.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace FilesTestHelpers
{
using Bytes = std::vector<std::uint8_t>;
using Channels = std::vector<std::vector<float>>;

namespace Files = MakeASound::Files;

// Appends numbers and text in either byte order.
struct ByteWriter
{
    ByteWriter& u8(std::uint32_t value)
    {
        bytes.push_back((std::uint8_t) value);
        return *this;
    }

    ByteWriter& le(std::uint64_t value, int numBytes)
    {
        for (auto i = 0; i < numBytes; ++i)
            u8((std::uint32_t) (value >> (8 * i)) & 0xff);

        return *this;
    }

    ByteWriter& be(std::uint64_t value, int numBytes)
    {
        for (auto i = numBytes - 1; i >= 0; --i)
            u8((std::uint32_t) (value >> (8 * i)) & 0xff);

        return *this;
    }

    ByteWriter& u16(std::uint32_t value) { return le(value, 2); }
    ByteWriter& u32(std::uint64_t value) { return le(value, 4); }
    ByteWriter& u64(std::uint64_t value) { return le(value, 8); }
    ByteWriter& u16be(std::uint32_t value) { return be(value, 2); }
    ByteWriter& u32be(std::uint64_t value) { return be(value, 4); }

    ByteWriter& f32(float value) { return u32(std::bit_cast<std::uint32_t>(value)); }
    ByteWriter& f64(double value) { return u64(std::bit_cast<std::uint64_t>(value)); }

    ByteWriter& f32be(float value)
    {
        return be(std::bit_cast<std::uint32_t>(value), 4);
    }

    ByteWriter& f64be(double value)
    {
        return be(std::bit_cast<std::uint64_t>(value), 8);
    }

    ByteWriter& text(std::string_view value)
    {
        for (auto c: value)
            u8((std::uint8_t) c);

        return *this;
    }

    ByteWriter& zeros(int count)
    {
        for (auto i = 0; i < count; ++i)
            u8(0);

        return *this;
    }

    ByteWriter& append(const Bytes& more)
    {
        bytes.insert(bytes.end(), more.begin(), more.end());
        return *this;
    }

    Bytes bytes;
};

// id, u32 LE size (the payload's unless given), payload, and the pad byte for an
// odd payload.
inline Bytes riffChunk(std::string_view id,
                       const Bytes& payload,
                       std::int64_t sizeField = -1)
{
    auto out = ByteWriter {};
    out.text(id.substr(0, 4))
        .u32(sizeField < 0 ? (std::uint64_t) payload.size() : (std::uint64_t) sizeField)
        .append(payload);

    if (payload.size() % 2 != 0)
        out.u8(0);

    return out.bytes;
}

// The same with a big-endian size, for AIFF.
inline Bytes aiffChunk(std::string_view id,
                       const Bytes& payload,
                       std::int64_t sizeField = -1)
{
    auto out = ByteWriter {};
    out.text(id.substr(0, 4))
        .u32be(sizeField < 0 ? (std::uint64_t) payload.size() : (std::uint64_t) sizeField)
        .append(payload);

    if (payload.size() % 2 != 0)
        out.u8(0);

    return out.bytes;
}

// A plain 16-byte format payload with consistent average and block-align fields.
inline Bytes plainFormat(int tag, int channels, std::uint32_t rate, int bits)
{
    auto align = channels * bits / 8;
    auto out = ByteWriter {};
    out.u16(tag).u16(channels).u32(rate).u32((std::uint64_t) rate * align).u16(align).u16(bits);
    return out.bytes;
}

inline constexpr std::uint8_t pcmGuid[16] = {
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
    0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71};

inline constexpr std::uint8_t floatGuid[16] = {
    0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
    0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71};

// The 40-byte extensible format payload.
inline Bytes extensibleFormat(int channels,
                              std::uint32_t rate,
                              int bits,
                              std::uint32_t mask,
                              const std::uint8_t (&guid)[16],
                              int validBits = -1)
{
    auto out = ByteWriter {};
    out.append(plainFormat(0xfffe, channels, rate, bits))
        .u16(22)
        .u16(validBits < 0 ? bits : validBits)
        .u32(mask);

    for (auto b: guid)
        out.u8(b);

    return out.bytes;
}

// "RIFF", size (the real one unless given), "WAVE", the chunks.
inline Bytes riffFile(const std::vector<Bytes>& chunks, std::int64_t sizeField = -1)
{
    auto body = ByteWriter {};
    body.text("WAVE");

    for (auto& chunk: chunks)
        body.append(chunk);

    auto out = ByteWriter {};
    out.text("RIFF")
        .u32(sizeField < 0 ? (std::uint64_t) body.bytes.size() : (std::uint64_t) sizeField)
        .append(body.bytes);
    return out.bytes;
}

// "FORM", BE size, the form type, the chunks.
inline Bytes aiffFile(std::string_view formType, const std::vector<Bytes>& chunks)
{
    auto body = ByteWriter {};
    body.text(formType);

    for (auto& chunk: chunks)
        body.append(chunk);

    auto out = ByteWriter {};
    out.text("FORM").u32be(body.bytes.size()).append(body.bytes);
    return out.bytes;
}

inline std::uint32_t readU32(const Bytes& bytes, std::size_t offset)
{
    return (std::uint32_t) bytes[offset] | ((std::uint32_t) bytes[offset + 1] << 8)
           | ((std::uint32_t) bytes[offset + 2] << 16)
           | ((std::uint32_t) bytes[offset + 3] << 24);
}

inline std::uint16_t readU16(const Bytes& bytes, std::size_t offset)
{
    return (std::uint16_t) (bytes[offset] | (bytes[offset + 1] << 8));
}

inline std::uint64_t readU64(const Bytes& bytes, std::size_t offset)
{
    return (std::uint64_t) readU32(bytes, offset)
           | ((std::uint64_t) readU32(bytes, offset + 4) << 32);
}

inline std::string textAt(const Bytes& bytes, std::size_t offset, std::size_t count)
{
    return std::string(bytes.begin() + (std::ptrdiff_t) offset,
                       bytes.begin() + (std::ptrdiff_t) (offset + count));
}

inline Bytes slice(const Bytes& bytes, std::size_t offset, std::size_t count)
{
    return Bytes(bytes.begin() + (std::ptrdiff_t) offset,
                 bytes.begin() + (std::ptrdiff_t) (offset + count));
}

inline std::uint32_t bitsOf(float value)
{
    return std::bit_cast<std::uint32_t>(value);
}

inline float floatOf(std::uint32_t bits)
{
    return std::bit_cast<float>(bits);
}

inline std::uint64_t fnv1a64(const Bytes& bytes)
{
    auto hash = std::uint64_t {0xcbf29ce484222325ull};

    for (auto byte: bytes)
    {
        hash ^= byte;
        hash *= 0x100000001b3ull;
    }

    return hash;
}

// The generator the pinned WAV files' input was made with.
struct Lcg
{
    explicit Lcg(std::uint32_t seed)
        : state(seed)
    {
    }

    std::uint32_t next()
    {
        state = state * 1664525u + 1013904223u;
        return state;
    }

    float nextBipolar() { return (float) ((double) next() / 2147483648.0 - 1.0); }

    std::uint32_t state;
};

// The pinned files' input: noise plus a ramp, with the boundary values 1, -1, 0
// (and 1.5, -1.5, 1e-9, 2^-16 when there is room) at the start of each channel.
inline Channels synthetic(int numChannels, int numFrames, std::uint32_t seed)
{
    auto lcg = Lcg(seed);
    auto channels = Channels((std::size_t) numChannels,
                             std::vector<float>((std::size_t) numFrames));

    for (auto& data: channels)
    {
        for (auto frame = 0; frame < numFrames; ++frame)
        {
            auto noise = 0.5f * lcg.nextBipolar();
            auto ramp = 0.25f * (float) frame / (float) numFrames;
            data[(std::size_t) frame] = noise + ramp;
        }

        data[0] = 1.f;
        data[1] = -1.f;
        data[2] = 0.f;

        if (numFrames > 6)
        {
            data[3] = 1.5f;
            data[4] = -1.5f;
            data[5] = 1e-9f;
            data[6] = 0.5f / 32768.f;
        }
    }

    return channels;
}

inline std::vector<const float*> pointersTo(const Channels& channels)
{
    auto pointers = std::vector<const float*>();

    for (auto& channel: channels)
        pointers.push_back(channel.data());

    return pointers;
}

// Opens a writer on a fresh MemorySink, writes every frame, finishes, and returns
// the file's bytes (empty if any step failed).
inline Bytes writeToMemory(const Files::WavWriterOptions& options,
                           const Channels& channels)
{
    auto sink = Files::MemorySink();
    auto ok = false;

    {
        auto writer = Files::WavWriter();

        if (writer.open(sink, options))
        {
            auto pointers = pointersTo(channels);
            auto numFrames = channels.empty() ? 0 : (int) channels[0].size();
            ok = writer.write(pointers.data(), numFrames);
            ok = writer.finish() && ok;
        }
    }

    return ok ? sink.takeData() : Bytes();
}

// Every frame of an open reader, as float, one vector per channel.
template <typename Reader>
Channels readAll(Reader& reader)
{
    auto channels = Channels((std::size_t) reader.numChannels(),
                             std::vector<float>((std::size_t) reader.numFrames()));
    auto pointers = std::vector<float*>();

    for (auto& channel: channels)
        pointers.push_back(channel.data());

    nano::check(reader.read(0, (int) reader.numFrames(), pointers.data()));
    return channels;
}

template <typename Reader>
std::vector<std::vector<std::int32_t>> readAllInt32(Reader& reader)
{
    auto channels = std::vector<std::vector<std::int32_t>>(
        (std::size_t) reader.numChannels(),
        std::vector<std::int32_t>((std::size_t) reader.numFrames()));
    auto pointers = std::vector<std::int32_t*>();

    for (auto& channel: channels)
        pointers.push_back(channel.data());

    nano::check(reader.readInt32(0, (int) reader.numFrames(), pointers.data()));
    return channels;
}

// Bitwise equality, so NaNs and signed zeros compare as stored.
inline bool sameBits(const Channels& a, const Channels& b)
{
    if (a.size() != b.size())
        return false;

    for (std::size_t c = 0; c < a.size(); ++c)
    {
        if (a[c].size() != b[c].size())
            return false;

        for (std::size_t i = 0; i < a[c].size(); ++i)
            if (bitsOf(a[c][i]) != bitsOf(b[c][i]))
                return false;
    }

    return true;
}
} // namespace FilesTestHelpers
