#include "VST3Common.h"
#include "Text.h"

#include "public.sdk/source/vst/utility/stringconvert.h"

#include <algorithm>

namespace MakeASound::VST3
{

namespace
{
constexpr auto maxUnits = 127;

bool isHighSurrogate(char16_t unit) noexcept
{
    return unit >= 0xd800 && unit <= 0xdbff;
}
} // namespace

void copyTo(Vst::String128 destination, std::string_view utf8)
{
    auto utf16 = std::u16string {};

    // The SDK's converter throws on malformed UTF-8; a host gets an empty string.
    try
    {
        utf16 = Vst::StringConvert::convert(std::string(utf8));
    }
    catch (...)
    {
    }

    auto length = std::min(static_cast<int>(utf16.size()), maxUnits);

    if (length < static_cast<int>(utf16.size()) && length > 0
        && isHighSurrogate(utf16[static_cast<size_t>(length - 1)]))
        --length;

    std::copy_n(utf16.data(), length, destination);
    destination[length] = 0;
}

std::string toUtf8(const Vst::TChar* text)
{
    if (text == nullptr)
        return {};

    try
    {
        return Vst::StringConvert::convert(text);
    }
    catch (...)
    {
        return {};
    }
}

} // namespace MakeASound::VST3
