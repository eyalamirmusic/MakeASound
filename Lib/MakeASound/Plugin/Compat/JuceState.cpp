#include "JuceState.h"
#include "JuceTree.h"

#include <array>
#include <cstring>

namespace MakeASound::Compat
{

namespace
{
constexpr auto privateDataMagic = std::string_view {"JUCEPrivateData"};

// One code point as CharPointer_UTF8::getAndAdvance decodes it, malformed
// sequences included, so a hash of any bytes matches JUCE's.
uint32_t nextCodePoint(std::string_view text, size_t& position) noexcept
{
    auto lead = static_cast<uint8_t>(text[position++]);

    if (lead < 0x80)
        return lead;

    auto value = uint32_t {lead};
    auto mask = uint32_t {0x7f};
    auto bit = uint32_t {0x40};
    auto extra = 0;

    while ((value & bit) != 0 && bit > 0x8)
    {
        mask >>= 1;
        ++extra;
        bit >>= 1;
    }

    value &= mask;

    while (--extra >= 0 && position < text.size())
    {
        auto next = static_cast<uint8_t>(text[position]);

        if ((next & 0xc0) != 0x80)
            break;

        ++position;
        value = (value << 6) | (next & 0x3f);
    }

    return value;
}

template <typename T, T multiplier>
T hashOf(std::string_view utf8) noexcept
{
    auto result = T {0};
    auto position = size_t {0};

    while (position < utf8.size() && utf8[position] != '\0')
        result = multiplier * result + static_cast<T>(nextCodePoint(utf8, position));

    return result;
}

uint32_t littleU32(std::string_view bytes, size_t at) noexcept
{
    auto value = uint32_t {0};

    for (auto i = size_t {0}; i < 4; ++i)
        value |= static_cast<uint32_t>(static_cast<uint8_t>(bytes[at + i]))
                 << (8 * i);

    return value;
}

uint64_t littleU64(std::string_view bytes, size_t at) noexcept
{
    auto value = uint64_t {0};

    for (auto i = size_t {0}; i < 8; ++i)
        value |= static_cast<uint64_t>(static_cast<uint8_t>(bytes[at + i]))
                 << (8 * i);

    return value;
}

constexpr int32_t fourCC(const char (&code)[5]) noexcept
{
    return static_cast<int32_t>(
        static_cast<uint32_t>(static_cast<uint8_t>(code[0])) << 24
        | static_cast<uint32_t>(static_cast<uint8_t>(code[1])) << 16
        | static_cast<uint32_t>(static_cast<uint8_t>(code[2])) << 8
        | static_cast<uint32_t>(static_cast<uint8_t>(code[3])));
}

// vst2persistence.cpp's magics.
constexpr auto cMagic = fourCC("CcnK");
constexpr auto bankMagic = fourCC("FxBk");
constexpr auto privateChunkId = fourCC("VstW");
constexpr auto chunkBankMagic = fourCC("FBCh");
constexpr auto programMagic = fourCC("FxCk");
constexpr auto chunkProgramMagic = fourCC("FPCh");

// IO::BigEndianStream over a view: a failed read leaves the reader failed.
class BigEndianReader
{
public:
    explicit BigEndianReader(std::string_view bytesToUse)
        : bytes(bytesToUse)
    {
    }

    std::optional<int32_t> i32()
    {
        auto raw = take(4);

        if (!raw)
            return std::nullopt;

        auto value = uint32_t {0};

        for (auto c: *raw)
            value = (value << 8) | static_cast<uint8_t>(c);

        return static_cast<int32_t>(value);
    }

    std::optional<float> f32()
    {
        auto bits = i32();

        if (!bits)
            return std::nullopt;

        auto value = 0.f;
        std::memcpy(&value, &*bits, sizeof value);
        return value;
    }

    std::optional<std::string_view> take(size_t count)
    {
        if (count > bytes.size() - position)
            return std::nullopt;

        auto taken = bytes.substr(position, count);
        position += count;
        return taken;
    }

private:
    std::string_view bytes;
    size_t position = 0;
};

std::optional<Vst2Program> readProgram(BigEndianReader& reader)
{
    auto id = reader.i32();
    auto size = id && *id == cMagic ? reader.i32() : std::nullopt;
    auto magic = size ? reader.i32() : std::nullopt;

    if (!magic || (*magic != programMagic && *magic != chunkProgramMagic))
        return std::nullopt;

    auto formatVersion = reader.i32();
    auto fxId = formatVersion ? reader.i32() : std::nullopt;
    auto fxVersion = fxId ? reader.i32() : std::nullopt;
    auto numParams = fxVersion ? reader.i32() : std::nullopt;
    auto name = numParams && *numParams >= 0 ? reader.take(28) : std::nullopt;

    if (!name)
        return std::nullopt;

    auto program = Vst2Program {};
    program.name = std::string {name->substr(0, name->find('\0'))};

    if (*magic == chunkProgramMagic)
    {
        auto chunkSize = reader.i32();
        auto chunk = chunkSize ? reader.take(static_cast<uint32_t>(*chunkSize))
                               : std::nullopt;

        if (!chunk)
            return std::nullopt;

        program.chunk = *chunk;
        return program;
    }

    for (auto i = 0; i < *numParams; ++i)
    {
        auto value = reader.f32();

        if (!value)
            return std::nullopt;

        program.values.add(*value);
    }

    return program;
}

// The 64-entry alphabet of juce_MemoryBlock.cpp, indexed by character - 43.
constexpr auto base64Decoding = std::array<int8_t, 80> {
    63, 0,  0,  0,  0,  53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 0,  0,  0,  0,  0,
    0,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18,
    19, 20, 21, 22, 23, 24, 25, 26, 0,  0,  0,  0,  0,  0,  27, 28, 29, 30, 31, 32,
    33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52};

// String::getIntValue: whitespace, a sign and the digits after it.
std::optional<long long> leadingInt(std::string_view text) noexcept
{
    auto position = size_t {0};

    while (position < text.size()
           && (text[position] == ' ' || text[position] == '\t'
               || text[position] == '\r' || text[position] == '\n'))
        ++position;

    auto negative = false;

    if (position < text.size() && (text[position] == '-' || text[position] == '+'))
        negative = text[position++] == '-';

    auto value = 0LL;

    while (position < text.size() && text[position] >= '0' && text[position] <= '9')
    {
        value = value * 10 + (text[position++] - '0');

        if (value > (1LL << 40))
            return std::nullopt;
    }

    return negative ? -value : value;
}

bool isTrue(const std::string& text) noexcept
{
    return text == "true" || juceStringToDouble(text) != 0.0;
}
} // namespace

uint32_t juceStringHash(std::string_view utf8) noexcept
{
    return hashOf<uint32_t, 31u>(utf8);
}

uint64_t juceStringHash64(std::string_view utf8) noexcept
{
    return hashOf<uint64_t, 101u>(utf8);
}

bool isJuceXmlBlob(std::string_view bytes) noexcept
{
    return bytes.size() > 8 && littleU32(bytes, 0) == 0x21324356u;
}

std::optional<std::string_view> juceXmlText(std::string_view bytes) noexcept
{
    if (!isJuceXmlBlob(bytes))
        return std::nullopt;

    auto length = static_cast<int32_t>(littleU32(bytes, 4));

    if (length <= 0)
        return std::nullopt;

    auto text = bytes.substr(8, static_cast<size_t>(length));
    return text.substr(0, text.find('\0'));
}

JucePrivateData stripJucePrivateData(std::string_view state)
{
    auto result = JucePrivateData {state, std::nullopt};
    auto magicSize = privateDataMagic.size();

    // Both i64s and the magic; JUCE checks for one i64 and reads the other
    // unchecked.
    if (state.size() < magicSize + 16 || !state.ends_with(privateDataMagic))
        return result;

    auto privateSize = littleU64(state, state.size() - magicSize - 8);

    // JUCE subtracts unchecked; a size that does not fit is no trailer here.
    if (privateSize > state.size() - magicSize - 16)
        return result;

    auto privateStart = state.size() - magicSize - 8 - privateSize;
    result.pluginState = state.substr(0, privateStart - 8);

    if (privateSize > 0)
    {
        auto tree = readJuceValueTree(
            state.substr(privateStart, static_cast<size_t>(privateSize)));
        const auto* bypass = tree ? tree->property("Bypass") : nullptr;
        result.bypass = bypass != nullptr && isTrue(*bypass);
    }

    return result;
}

std::optional<Vst2Bank> readVst2Bank(std::string_view bytes)
{
    try
    {
        auto reader = BigEndianReader {bytes};
        auto bank = Vst2Bank {};
        auto id = reader.i32();

        if (id && *id == privateChunkId)
        {
            auto size = reader.i32();
            auto version = size ? reader.i32() : std::nullopt;
            auto bypass = version ? reader.i32() : std::nullopt;

            if (!bypass)
                return std::nullopt;

            bank.bypass = *bypass != 0;
            id = reader.i32();
        }

        auto size = id && *id == cMagic ? reader.i32() : std::nullopt;
        auto magic = size ? reader.i32() : std::nullopt;

        if (!magic || (*magic != bankMagic && *magic != chunkBankMagic))
            return std::nullopt;

        auto version = reader.i32();
        auto fxId = version ? reader.i32() : std::nullopt;
        auto fxVersion = fxId ? reader.i32() : std::nullopt;
        auto numPrograms = fxVersion ? reader.i32() : std::nullopt;

        if (!numPrograms || (*numPrograms < 1 && *magic == bankMagic))
            return std::nullopt;

        bank.fxUniqueId = *fxId;
        bank.fxVersion = *fxVersion;

        if (*version >= 1)
        {
            auto current = reader.i32();

            if (!current || !reader.take(124))
                return std::nullopt;

            bank.currentProgram = *current;
        }

        if (*magic == bankMagic)
        {
            for (auto i = 0; i < *numPrograms; ++i)
            {
                auto program = readProgram(reader);

                if (!program)
                    return std::nullopt;

                bank.programs.add(std::move(*program));
            }

            return bank;
        }

        auto chunkSize = reader.i32();

        if (!chunkSize || *chunkSize == 0)
            return std::nullopt;

        auto chunk = reader.take(static_cast<uint32_t>(*chunkSize));

        if (!chunk)
            return std::nullopt;

        bank.chunk = *chunk;
        return bank;
    }
    catch (...)
    {
        return std::nullopt;
    }
}

ForeignState juceVST3State(std::string_view state, StateContext context)
{
    auto result = ForeignState {};
    result.origin = ForeignOrigin::JuceVST3;
    result.bytes = state;
    result.context = context;

    if (auto bank = readVst2Bank(state))
    {
        result.origin = ForeignOrigin::JuceVST2Bank;
        result.program = bank->currentProgram;
        result.bypass = bank->bypass;

        // Values, not a chunk: nothing JUCE would hand its plugin, so the plugin
        // gets the whole bank to read itself.
        if (bank->chunk.empty())
            return result;

        result.bytes = bank->chunk;
    }

    auto stripped = stripJucePrivateData(result.bytes);
    result.bytes = stripped.pluginState;

    if (stripped.bypass)
        result.bypass = stripped.bypass;

    return result;
}

std::optional<std::string> fromJuceBase64(std::string_view text)
{
    try
    {
        text = text.substr(0, text.find('\0'));
        auto dot = text.find('.');

        if (dot == std::string_view::npos)
            return std::nullopt;

        auto count = leadingInt(text.substr(0, dot));
        auto characters = text.substr(dot + 1);

        // Refused rather than zero-filled: no JUCE writer leaves the count past
        // what its characters carry.
        if (!count || *count < 0
            || static_cast<size_t>(*count) > characters.size() * 6 / 8 + 1)
            return std::nullopt;

        auto bytes = std::string(static_cast<size_t>(*count), '\0');
        auto bit = size_t {0};

        for (auto c: characters)
        {
            auto index = static_cast<int>(static_cast<uint8_t>(c)) - 43;

            if (index < 0 || index >= static_cast<int>(base64Decoding.size()))
                continue;

            auto sixBits = base64Decoding[static_cast<size_t>(index)];

            for (auto i = 0; i < 6; ++i, ++bit)
                if (((sixBits >> i) & 1) != 0 && bit / 8 < bytes.size())
                    bytes[bit / 8] |= static_cast<char>(1u << (bit % 8));
        }

        return bytes;
    }
    catch (...)
    {
        return std::nullopt;
    }
}

std::string toJuceBase64(std::string_view bytes)
{
    constexpr auto alphabet = std::string_view {
        ".ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+"};

    auto numChars = (bytes.size() * 8 + 5) / 6;
    auto result = std::to_string(bytes.size());
    result.reserve(result.size() + 1 + numChars);
    result += '.';

    for (auto i = size_t {0}; i < numChars; ++i)
    {
        auto sixBits = 0;

        for (auto b = 0; b < 6; ++b)
        {
            auto bit = i * 6 + static_cast<size_t>(b);

            if (bit / 8 < bytes.size()
                && ((static_cast<uint8_t>(bytes[bit / 8]) >> (bit % 8)) & 1) != 0)
                sixBits |= 1 << b;
        }

        result += alphabet[static_cast<size_t>(sixBits)];
    }

    return result;
}

std::optional<JuceVST3HostState> juceVST3HostState(std::string_view bytes)
{
    auto text = juceXmlText(bytes);
    auto document = text ? parseJuceXml(*text) : std::nullopt;

    if (!document || document->type != "VST3PluginState")
        return std::nullopt;

    auto decoded = [&](std::string_view name) -> std::optional<std::string>
    {
        const auto* child = document->child(name);
        return child != nullptr ? fromJuceBase64(child->text) : std::nullopt;
    };

    auto state = JuceVST3HostState {};
    state.component = decoded("IComponent").value_or(std::string {});
    state.controller = decoded("IEditController");
    return state;
}

std::string toJuceVST3HostState(const JuceVST3HostState& state)
{
    // XmlElement::writeTo with TextFormat().singleLine(): the declaration, one
    // space, the element; base64 text needs no escaping.
    auto text = std::string {"<?xml version=\"1.0\" encoding=\"UTF-8\"?> "
                             "<VST3PluginState><IComponent>"};
    text += toJuceBase64(state.component);
    text += "</IComponent>";

    if (state.controller && !state.controller->empty())
    {
        text += "<IEditController>";
        text += toJuceBase64(*state.controller);
        text += "</IEditController>";
    }

    text += "</VST3PluginState>";

    // copyXmlToBinary: the magic, the length (the whole size less the 8-byte
    // header and the NUL), the text, the NUL.
    auto length = static_cast<uint32_t>(text.size());
    auto result = std::string {"VC2!"};

    for (auto i = 0; i < 4; ++i)
        result += static_cast<char>((length >> (8 * i)) & 0xff);

    result += text;
    result += '\0';
    return result;
}

std::optional<std::string> juceSettingsValue(std::string_view propertiesXml,
                                             std::string_view name)
{
    auto document = parseJuceXml(propertiesXml);

    if (!document || document->type != "PROPERTIES")
        return std::nullopt;

    auto found = std::optional<std::string> {};

    for (const auto& entry: document->children)
    {
        const auto* key = entry.property("name");
        const auto* value = entry.property("val");

        if (entry.type == "VALUE" && key != nullptr && *key == name)
            found = value != nullptr ? *value : std::string {};
    }

    return found;
}

std::optional<std::string> juceStandaloneState(std::string_view propertiesXml)
{
    auto encoded = juceSettingsValue(propertiesXml, "filterState");
    auto decoded = encoded ? fromJuceBase64(*encoded) : std::nullopt;

    if (!decoded || decoded->empty())
        return std::nullopt;

    return decoded;
}

} // namespace MakeASound::Compat
