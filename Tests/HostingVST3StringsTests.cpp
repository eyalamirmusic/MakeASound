// The VST3 host's text conversion (Hosting/VST3/VST3Strings.h) on its own, over
// crafted input: UTF-16 with lone surrogates, no terminator or a full String128,
// and UTF-8 that is malformed every way Unicode names. Nothing throws, each broken
// sequence is one U+FFFD, and nothing is read or written past the buffer.

#include <MakeASound/Hosting/VST3/VST3Strings.h>

#include <NanoTest/NanoTest.h>

#include <algorithm>
#include <array>
#include <string>

namespace HostingVST3StringsTests
{
using namespace nano;
using namespace MakeASound::Hosting;

namespace
{
constexpr auto high = char16_t {0xd83d};
constexpr auto low = char16_t {0xde00};
constexpr auto eAcute = char16_t {0x00e9};
constexpr auto euro = char16_t {0x20ac};

// U+FFFD, U+1F600 (the pair above) and U+00E9 U+20AC in UTF-8, and U+FFFD in
// UTF-16.
const auto replacement = std::string {"\xEF\xBF\xBD"};
const auto smile = std::string {"\xF0\x9F\x98\x80"};
const auto eAcuteEuro = std::string {"\xC3\xA9\xE2\x82\xAC"};
const auto r = std::u16string {char16_t {0xfffd}};

// A String128 holding exactly these units, then terminators.
using String128 = std::array<char16_t, 128>;

String128 string128(const std::u16string& units)
{
    auto result = String128 {};
    std::copy_n(
        units.begin(), std::min(units.size(), result.size()), result.begin());
    return result;
}

std::string toUtf8(const String128& text)
{
    return VST3Strings::toUtf8(text.data(), static_cast<int>(text.size()));
}
} // namespace

auto tSurrogates = test("HostingVST3Strings/aLoneSurrogateReadsAsReplacement") = []
{
    check(toUtf8(string128({u'A', high})) == "A" + replacement);
    check(toUtf8(string128({low, u'B'})) == replacement + "B");
    check(toUtf8(string128({high, u'C', low})) == replacement + "C" + replacement);

    // Reversed, the two halves are two lone surrogates.
    check(toUtf8(string128({low, high})) == replacement + replacement);
    check(toUtf8(string128({high, high, low})) == replacement + smile);

    // A pair is one character, in four bytes.
    check(toUtf8(string128({u'x', high, low, u'y'})) == "x" + smile + "y");
    check(toUtf8(string128({eAcute, euro})) == eAcuteEuro);
};

auto tBounds = test("HostingVST3Strings/aStringIsReadNoFurtherThanItsSize") = []
{
    // 128 units and no terminator: all 128, and not one past.
    auto full = std::array<char16_t, 129> {};
    std::fill(full.begin(), full.end(), u'f');
    check(VST3Strings::toUtf8(full.data(), 128) == std::string(128, 'f'));

    // The fixed-size overload reads the array's own size.
    char16_t array[128];
    std::fill(std::begin(array), std::end(array), u'a');
    check(VST3Strings::toUtf8(array) == std::string(128, 'a'));

    // A pair split by the end reads as a lone surrogate.
    array[127] = high;
    check(VST3Strings::toUtf8(array) == std::string(127, 'a') + replacement);

    // A terminator ends it early; null and a non-positive size are empty.
    array[3] = 0;
    check(VST3Strings::toUtf8(array) == "aaa");
    check(VST3Strings::toUtf8(nullptr, 128).empty());
    check(VST3Strings::toUtf8(array, 0).empty());
    check(VST3Strings::toUtf8(array, -1).empty());
};

auto tMalformed = test("HostingVST3Strings/malformedUtf8ReadsAsReplacement") = []
{
    auto u = [](std::string bytes) { return VST3Strings::toUtf16(bytes); };

    // Well formed, one to four bytes, and nothing.
    check(u("").empty());
    check(u("az") == u"az");
    check(u(eAcuteEuro) == std::u16string {eAcute, euro});
    check(u("x" + smile) == std::u16string {u'x', high, low});

    // A lone lead byte, a continuation with no lead, bytes no UTF-8 has.
    check(u("\xC3") == r);
    check(u("\x80") == r);
    check(u("\xFF\xFE") == r + r);
    check(u("\xC0\xAF") == r + r);

    // A sequence cut short is one U+FFFD however much of it there was, and the
    // byte that broke it starts afresh.
    check(u("\xF0\x9F\x98") == r);
    check(u(std::string {"\xF0\x9F\x98"} + "A") == r + u"A");
    check(u(std::string {"\xE2\x82"} + eAcuteEuro) == r + eAcute + euro);

    // Overlong, an encoded surrogate, past U+10FFFF: each byte on its own.
    check(u("\xE0\x80\xAF") == r + r + r);
    check(u("\xED\xA0\x80") == r + r + r);
    check(u("\xF4\x90\x80\x80") == r + r + r + r);

    // The highest of each length still reads.
    check(u("\xDF\xBF") == std::u16string {char16_t {0x07ff}});
    check(u("\xEF\xBF\xBF") == std::u16string {char16_t {0xffff}});
    check(u("\xF4\x8F\xBF\xBF")
          == std::u16string {char16_t {0xdbff}, char16_t {0xdfff}});

    // Embedded nulls are kept, as the input has them.
    check(u(std::string {"a\0b", 3}) == std::u16string {u'a', 0, u'b'});
};

auto tCopy = test("HostingVST3Strings/aCopyIsTerminatedAndNeverSplitsAPair") = []
{
    auto buffer = std::array<char16_t, 8> {};
    auto copy = [&](std::string_view utf8, int capacity)
    {
        std::fill(buffer.begin(), buffer.end(), u'#');
        VST3Strings::copyTo(buffer.data(), capacity, utf8);
        return std::u16string(buffer.data());
    };

    check(copy("abc", 8) == u"abc");
    check(copy("abcdefgh", 8) == u"abcdefg");
    check(copy("abcdefg", 8) == u"abcdefg");
    check(buffer[7] == 0);

    // The pair would straddle the last unit, so it is left out whole.
    check(copy("abcdef" + smile, 8) == u"abcdef");
    check(copy("abcde" + smile, 8) == u"abcde" + std::u16string {high, low});

    // Malformed input is copied with its U+FFFD, not refused.
    check(copy("x\xFFy", 8) == u"x" + r + u"y");

    // One unit holds the terminator only; none writes nothing.
    check(copy("abc", 1).empty());
    std::fill(buffer.begin(), buffer.end(), u'#');
    VST3Strings::copyTo(buffer.data(), 0, "abc");
    check(buffer[0] == u'#');
    VST3Strings::copyTo(nullptr, 8, "abc");

    // A String128's room: 127 units and the terminator.
    char16_t name[VST3Strings::string128Size];
    VST3Strings::copyTo(name, VST3Strings::string128Size, std::string(300, 'n'));
    check(VST3Strings::toUtf8(name) == std::string(127, 'n'));
    check(name[127] == 0);
};

auto tRoundTrip = test("HostingVST3Strings/wellFormedTextRoundTrips") = []
{
    for (auto text: {std::string {"Gain"},
                     std::string {"\xC3\xA9t\xC3\xA9 \xE2\x82\xAC"},
                     "Smile " + smile,
                     std::string {"\xF4\x8F\xBF\xBF"}})
    {
        auto utf16 = VST3Strings::toUtf16(text);
        check(VST3Strings::toUtf8(utf16.c_str(), static_cast<int>(utf16.size()))
              == text);
    }
};
} // namespace HostingVST3StringsTests
