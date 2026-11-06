// No SDK header, so no VST3Common.h either: nothing here can reach the
// setState/getState rename, in a unity chunk or out of one.
#include "VST3Strings.h"

#include <algorithm>

namespace MakeASound::Hosting::VST3Strings
{

namespace
{
constexpr auto replacement = char32_t {0xfffd};

bool isHighSurrogate(char32_t unit) noexcept
{
    return unit >= 0xd800 && unit <= 0xdbff;
}

bool isLowSurrogate(char32_t unit) noexcept
{
    return unit >= 0xdc00 && unit <= 0xdfff;
}

void appendUtf8(std::string& out, char32_t codePoint)
{
    auto byte = [&](char32_t value) { out += static_cast<char>(value); };

    if (codePoint < 0x80)
    {
        byte(codePoint);
    }
    else if (codePoint < 0x800)
    {
        byte(0xc0 | (codePoint >> 6));
        byte(0x80 | (codePoint & 0x3f));
    }
    else if (codePoint < 0x10000)
    {
        byte(0xe0 | (codePoint >> 12));
        byte(0x80 | ((codePoint >> 6) & 0x3f));
        byte(0x80 | (codePoint & 0x3f));
    }
    else
    {
        byte(0xf0 | (codePoint >> 18));
        byte(0x80 | ((codePoint >> 12) & 0x3f));
        byte(0x80 | ((codePoint >> 6) & 0x3f));
        byte(0x80 | (codePoint & 0x3f));
    }
}

void appendUtf16(std::u16string& out, char32_t codePoint)
{
    if (codePoint < 0x10000)
    {
        out += static_cast<char16_t>(codePoint);
        return;
    }

    codePoint -= 0x10000;
    out += static_cast<char16_t>(0xd800 + (codePoint >> 10));
    out += static_cast<char16_t>(0xdc00 + (codePoint & 0x3ff));
}

// The length of the sequence a lead byte starts (0 when it cannot start one), and
// the range its second byte must fall in. The ranges are what rule out overlong
// forms, encoded surrogates and anything past U+10FFFF (Unicode's table 3-7).
struct Lead
{
    int length = 0;
    unsigned char low = 0x80;
    unsigned char high = 0xbf;
};

Lead leadOf(unsigned char byte) noexcept
{
    if (byte < 0x80)
        return {1};

    if (byte >= 0xc2 && byte <= 0xdf)
        return {2};

    if (byte == 0xe0)
        return {3, 0xa0, 0xbf};

    if (byte == 0xed)
        return {3, 0x80, 0x9f};

    if (byte >= 0xe1 && byte <= 0xef)
        return {3};

    if (byte == 0xf0)
        return {4, 0x90, 0xbf};

    if (byte >= 0xf1 && byte <= 0xf3)
        return {4};

    if (byte == 0xf4)
        return {4, 0x80, 0x8f};

    return {};
}
} // namespace

std::string toUtf8(const char16_t* text, int maxUnits)
{
    auto result = std::string();

    if (text == nullptr)
        return result;

    auto length = 0;

    while (length < maxUnits && text[length] != 0)
        ++length;

    result.reserve(static_cast<size_t>(length));

    for (auto i = 0; i < length; ++i)
    {
        auto unit = static_cast<char32_t>(text[i]);

        if (isHighSurrogate(unit) && i + 1 < length && isLowSurrogate(text[i + 1]))
        {
            auto next = static_cast<char32_t>(text[++i]);
            appendUtf8(result, 0x10000 + ((unit - 0xd800) << 10) + (next - 0xdc00));
        }
        else if (isHighSurrogate(unit) || isLowSurrogate(unit))
        {
            appendUtf8(result, replacement);
        }
        else
        {
            appendUtf8(result, unit);
        }
    }

    return result;
}

std::u16string toUtf16(std::string_view utf8)
{
    auto result = std::u16string();
    result.reserve(utf8.size());

    while (!utf8.empty())
    {
        const auto first = static_cast<unsigned char>(utf8.front());
        const auto lead = leadOf(first);

        if (lead.length <= 1)
        {
            result += lead.length == 1 ? static_cast<char16_t>(first)
                                       : static_cast<char16_t>(replacement);
            utf8.remove_prefix(1);
            continue;
        }

        // The lead's own bits, then six per continuation byte. A sequence cut
        // short, by the end or a byte out of range, is one U+FFFD for what it
        // consumed, and the byte that broke it starts afresh.
        const auto available = static_cast<int>(std::min(utf8.size(), size_t {4}));
        auto codePoint = static_cast<char32_t>(first & (0xff >> (lead.length + 1)));
        auto consumed = 1;

        for (; consumed < lead.length && consumed < available; ++consumed)
        {
            const auto next = static_cast<unsigned char>(utf8[consumed]);
            const auto low = consumed == 1 ? lead.low : 0x80;
            const auto high = consumed == 1 ? lead.high : 0xbf;

            if (next < low || next > high)
                break;

            codePoint = (codePoint << 6) | (next & 0x3f);
        }

        utf8.remove_prefix(static_cast<size_t>(consumed));
        appendUtf16(result, consumed == lead.length ? codePoint : replacement);
    }

    return result;
}

void copyTo(char16_t* destination, int capacity, std::string_view utf8)
{
    if (destination == nullptr || capacity <= 0)
        return;

    const auto utf16 = toUtf16(utf8);
    const auto size =
        static_cast<int>(std::min(utf16.size(), static_cast<size_t>(capacity)));
    auto length = std::min(size, capacity - 1);

    if (length < size && length > 0
        && isHighSurrogate(utf16[static_cast<size_t>(length - 1)]))
        --length;

    std::copy_n(utf16.data(), length, destination);
    destination[length] = 0;
}

} // namespace MakeASound::Hosting::VST3Strings
