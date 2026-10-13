#include "JuceTree.h"

#include <cstdint>
#include <cstring>
#include <locale>
#include <sstream>

namespace MakeASound::Compat
{

namespace
{
// Deeper than any state JUCE writes, shallow enough for any thread's stack.
constexpr auto maxDepth = 256;

void appendUtf8(std::string& out, uint32_t codePoint)
{
    if (codePoint < 0x80)
        out += static_cast<char>(codePoint);
    else if (codePoint < 0x800)
    {
        out += static_cast<char>(0xc0 | (codePoint >> 6));
        out += static_cast<char>(0x80 | (codePoint & 0x3f));
    }
    else if (codePoint < 0x10000)
    {
        out += static_cast<char>(0xe0 | (codePoint >> 12));
        out += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3f));
        out += static_cast<char>(0x80 | (codePoint & 0x3f));
    }
    else
    {
        out += static_cast<char>(0xf0 | ((codePoint >> 18) & 0x07));
        out += static_cast<char>(0x80 | ((codePoint >> 12) & 0x3f));
        out += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3f));
        out += static_cast<char>(0x80 | (codePoint & 0x3f));
    }
}

bool isSpace(char c) noexcept
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

std::string formatDouble(double value)
{
    for (auto precision = 15; precision <= 17; ++precision)
    {
        auto stream = std::ostringstream {};
        stream.imbue(std::locale::classic());
        stream.precision(precision);
        stream << value;

        auto text = stream.str();

        if (juceStringToDouble(text) == value || precision == 17)
            return text;
    }

    return {};
}

// A cursor over XmlElement text. Every read is bounds-checked; a failure
// unwinds as nullopt.
class XmlReader
{
public:
    explicit XmlReader(std::string_view textToUse)
        : text(textToUse)
    {
    }

    std::optional<JuceTree> document()
    {
        skipBom();

        while (true)
        {
            skipSpace();

            if (startsWith("<?"))
            {
                if (!skipPast("?>"))
                    return std::nullopt;
            }
            else if (startsWith("<!--"))
            {
                if (!skipPast("-->"))
                    return std::nullopt;
            }
            else if (startsWith("<!"))
            {
                if (!skipDeclaration())
                    return std::nullopt;
            }
            else if (startsWith("<"))
                return element(0);
            else
                return std::nullopt;
        }
    }

private:
    std::optional<JuceTree> element(int depth)
    {
        if (depth > maxDepth || !consume('<'))
            return std::nullopt;

        auto tree = JuceTree {};
        tree.type = name();

        if (tree.type.empty())
            return std::nullopt;

        while (true)
        {
            skipSpace();

            if (atEnd())
                return std::nullopt;

            if (consume('/'))
                return consume('>') ? std::optional {std::move(tree)} : std::nullopt;

            if (consume('>'))
                break;

            auto attribute = name();
            skipSpace();

            if (attribute.empty() || !consume('='))
                return std::nullopt;

            skipSpace();
            auto value = quoted();

            if (!value)
                return std::nullopt;

            tree.properties.emplace_back(std::move(attribute), std::move(*value));
        }

        while (true)
        {
            if (atEnd())
                return std::nullopt;

            if (startsWith("</"))
                return skipPast(">") ? std::optional {std::move(tree)}
                                     : std::nullopt;

            if (startsWith("<!--"))
            {
                if (!skipPast("-->"))
                    return std::nullopt;
            }
            else if (startsWith("<![CDATA["))
            {
                if (!skipPast("]]>"))
                    return std::nullopt;
            }
            else if (startsWith("<?"))
            {
                if (!skipPast("?>"))
                    return std::nullopt;
            }
            else if (startsWith("<"))
            {
                auto child = element(depth + 1);

                if (!child)
                    return std::nullopt;

                tree.children.push_back(std::move(*child));
            }
            else
            {
                // Text, which a ValueTree has no place for.
                while (!atEnd() && text[position] != '<')
                    ++position;
            }
        }
    }

    std::string name()
    {
        auto start = position;

        while (!atEnd())
        {
            auto c = text[position];

            if (isSpace(c) || c == '/' || c == '>' || c == '=' || c == '<'
                || c == '"' || c == '\'')
                break;

            ++position;
        }

        return std::string {text.substr(start, position - start)};
    }

    std::optional<std::string> quoted()
    {
        if (atEnd() || (text[position] != '"' && text[position] != '\''))
            return std::nullopt;

        auto quote = text[position++];
        auto value = std::string {};

        while (!atEnd() && text[position] != quote)
        {
            if (text[position] == '&')
                entity(value);
            else
                value += text[position++];
        }

        if (!consume(quote))
            return std::nullopt;

        return value;
    }

    // At an '&': the reference decoded, or the '&' kept when it is none.
    void entity(std::string& out)
    {
        auto end = text.find(';', position);

        if (end == std::string_view::npos || end - position > 12)
        {
            out += text[position++];
            return;
        }

        auto body = text.substr(position + 1, end - position - 1);

        if (auto named = namedEntity(body))
            out += *named;
        else if (auto codePoint = characterReference(body))
            appendUtf8(out, *codePoint);
        else
        {
            out += text[position++];
            return;
        }

        position = end + 1;
    }

    static std::optional<char> namedEntity(std::string_view body) noexcept
    {
        if (body == "amp")
            return '&';
        if (body == "lt")
            return '<';
        if (body == "gt")
            return '>';
        if (body == "quot")
            return '"';
        if (body == "apos")
            return '\'';

        return std::nullopt;
    }

    static std::optional<uint32_t> characterReference(std::string_view body) noexcept
    {
        if (body.size() < 2 || body[0] != '#')
            return std::nullopt;

        auto hex = body[1] == 'x' || body[1] == 'X';
        auto digits = body.substr(hex ? 2 : 1);

        if (digits.empty())
            return std::nullopt;

        auto value = uint32_t {0};

        for (auto c: digits)
        {
            auto digit = 0u;

            if (c >= '0' && c <= '9')
                digit = static_cast<uint32_t>(c - '0');
            else if (hex && c >= 'a' && c <= 'f')
                digit = static_cast<uint32_t>(c - 'a' + 10);
            else if (hex && c >= 'A' && c <= 'F')
                digit = static_cast<uint32_t>(c - 'A' + 10);
            else
                return std::nullopt;

            value = value * (hex ? 16u : 10u) + digit;

            if (value > 0x10ffff)
                return std::nullopt;
        }

        return value;
    }

    // `<!DOCTYPE ...>`, an internal subset in brackets included.
    bool skipDeclaration()
    {
        auto brackets = 0;

        while (!atEnd())
        {
            auto c = text[position++];

            if (c == '[')
                ++brackets;
            else if (c == ']')
                --brackets;
            else if (c == '>' && brackets <= 0)
                return true;
        }

        return false;
    }

    void skipBom()
    {
        if (startsWith("\xef\xbb\xbf"))
            position += 3;
    }

    void skipSpace()
    {
        while (!atEnd() && isSpace(text[position]))
            ++position;
    }

    bool skipPast(std::string_view marker)
    {
        auto found = text.find(marker, position);

        if (found == std::string_view::npos)
            return false;

        position = found + marker.size();
        return true;
    }

    bool startsWith(std::string_view prefix) const noexcept
    {
        return text.substr(position).starts_with(prefix);
    }

    bool consume(char c) noexcept
    {
        if (atEnd() || text[position] != c)
            return false;

        ++position;
        return true;
    }

    bool atEnd() const noexcept { return position >= text.size(); }

    std::string_view text;
    size_t position = 0;
};

// The var markers of juce_Variant.cpp.
enum VarMarker : uint8_t
{
    varInt = 1,
    varBoolTrue = 2,
    varBoolFalse = 3,
    varDouble = 4,
    varString = 5,
    varInt64 = 6,
    varBinary = 8
};

// A cursor over ValueTree::writeToStream's bytes, little-endian as JUCE writes
// them on every platform.
class StreamReader
{
public:
    explicit StreamReader(std::string_view bytesToUse)
        : bytes(bytesToUse)
    {
    }

    std::optional<JuceTree> tree(int depth)
    {
        auto type = string();

        if (!type || type->empty() || depth > maxDepth)
            return std::nullopt;

        auto result = JuceTree {};
        result.type = std::move(*type);

        auto numProperties = compressedInt();

        if (!numProperties || *numProperties < 0)
            return std::nullopt;

        for (auto i = 0; i < *numProperties; ++i)
        {
            auto name = string();
            auto value = name ? var() : std::nullopt;

            if (!value)
                return std::nullopt;

            if (!name->empty())
                set(result, std::move(*name), std::move(*value));
        }

        auto numChildren = compressedInt();

        if (!numChildren || *numChildren < 0)
            return std::nullopt;

        for (auto i = 0; i < *numChildren; ++i)
        {
            auto child = tree(depth + 1);

            if (!child)
                return std::nullopt;

            result.children.push_back(std::move(*child));
        }

        return result;
    }

private:
    static void set(JuceTree& tree, std::string name, std::string value)
    {
        for (auto& [existing, text]: tree.properties)
        {
            if (existing == name)
            {
                text = std::move(value);
                return;
            }
        }

        tree.properties.emplace_back(std::move(name), std::move(value));
    }

    std::optional<std::string> var()
    {
        auto size = compressedInt();

        if (!size || *size < 0)
            return std::nullopt;

        // An empty var: JUCE reads it as void, which prints as nothing.
        if (*size == 0)
            return std::string {};

        auto marker = byte();

        if (!marker)
            return std::nullopt;

        switch (*marker)
        {
            case varInt:
            {
                auto value = little<uint32_t>();

                if (!value)
                    return std::nullopt;

                return std::to_string(static_cast<int32_t>(*value));
            }

            case varInt64:
            {
                auto value = little<uint64_t>();

                if (!value)
                    return std::nullopt;

                return std::to_string(static_cast<int64_t>(*value));
            }

            case varBoolTrue:
                return std::string {"1"};

            case varBoolFalse:
                return std::string {"0"};

            case varDouble:
            {
                auto value = little<uint64_t>();

                if (!value)
                    return std::nullopt;

                auto number = 0.0;
                std::memcpy(&number, &*value, sizeof number);
                return formatDouble(number);
            }

            case varString:
            {
                auto raw = take(static_cast<size_t>(*size) - 1);

                if (!raw)
                    return std::nullopt;

                return std::string {raw->substr(0, raw->find('\0'))};
            }

            case varBinary:
            {
                auto raw = take(static_cast<size_t>(*size) - 1);
                return raw ? std::optional {std::string {*raw}} : std::nullopt;
            }

            default:
            {
                // An array, an undefined or a marker from a later JUCE: JUCE
                // skips what it does not read, and so does this.
                auto raw = take(static_cast<size_t>(*size) - 1);
                return raw ? std::optional {std::string {}} : std::nullopt;
            }
        }
    }

    // OutputStream::writeCompressedInt: a byte holding the count of the
    // little-endian magnitude bytes that follow, its top bit the sign.
    std::optional<int> compressedInt()
    {
        auto sizeByte = byte();

        if (!sizeByte)
            return std::nullopt;

        auto numBytes = *sizeByte & 0x7f;

        if (numBytes > 4)
            return std::nullopt;

        auto magnitude = uint32_t {0};

        for (auto i = 0; i < numBytes; ++i)
        {
            auto next = byte();

            if (!next)
                return std::nullopt;

            magnitude |= static_cast<uint32_t>(*next) << (8 * i);
        }

        auto value = static_cast<int>(magnitude);
        return (*sizeByte & 0x80) != 0 ? -value : value;
    }

    std::optional<std::string> string()
    {
        auto end = bytes.find('\0', position);

        if (end == std::string_view::npos)
            return std::nullopt;

        auto text = std::string {bytes.substr(position, end - position)};
        position = end + 1;
        return text;
    }

    template <typename T>
    std::optional<T> little()
    {
        auto raw = take(sizeof(T));

        if (!raw)
            return std::nullopt;

        auto value = T {0};

        for (auto i = size_t {0}; i < sizeof(T); ++i)
            value |= static_cast<T>(static_cast<uint8_t>((*raw)[i])) << (8 * i);

        return value;
    }

    std::optional<uint8_t> byte()
    {
        auto raw = take(1);
        return raw ? std::optional {static_cast<uint8_t>((*raw)[0])} : std::nullopt;
    }

    std::optional<std::string_view> take(size_t count)
    {
        if (count > bytes.size() - position)
            return std::nullopt;

        auto taken = bytes.substr(position, count);
        position += count;
        return taken;
    }

    std::string_view bytes;
    size_t position = 0;
};
} // namespace

const std::string* JuceTree::property(std::string_view name) const noexcept
{
    const std::string* found = nullptr;

    for (const auto& [key, value]: properties)
        if (key == name)
            found = &value;

    return found;
}

const JuceTree* JuceTree::child(std::string_view childType) const noexcept
{
    for (const auto& node: children)
        if (node.type == childType)
            return &node;

    return nullptr;
}

std::optional<JuceTree> parseJuceXml(std::string_view text)
{
    try
    {
        return XmlReader {text}.document();
    }
    catch (...)
    {
        return std::nullopt;
    }
}

std::optional<JuceTree> readJuceValueTree(std::string_view bytes)
{
    try
    {
        return StreamReader {bytes}.tree(0);
    }
    catch (...)
    {
        return std::nullopt;
    }
}

double juceStringToDouble(std::string_view text) noexcept
{
    try
    {
        auto stream = std::istringstream {std::string {text}};
        stream.imbue(std::locale::classic());

        auto value = 0.0;
        stream >> value;
        return stream.fail() ? 0.0 : value;
    }
    catch (...)
    {
        return 0.0;
    }
}

} // namespace MakeASound::Compat
