#include "PathText.h"

#include <eacp/Core/Utils/FilePath.h>
#include <eacp/Core/Utils/Strings.h>

#include <windows.h>

namespace MakeASound::Hosting
{

namespace
{
std::string asciiUpperCased(std::string text)
{
    for (auto& character: text)
        if (character >= 'a' && character <= 'z')
            character = static_cast<char>(character - 'a' + 'A');

    return text;
}

bool isSurrogate(wchar_t unit) noexcept
{
    return unit >= 0xD800 && unit <= 0xDFFF;
}

// Upper case as NTFS compares names: one UTF-16 unit at a time, through the
// invariant locale, so the key never depends on the user's locale (a Turkish
// 'i' folds as 'I' everywhere). LCMapStringEx would also fold a surrogate pair
// (Deseret U+10428 to U+10400), which NTFS keeps apart, so those units are
// copied back unfolded. Text that is not valid UTF-8 is folded in its ASCII
// bytes only: decoding it would turn each bad byte into U+FFFD, and different
// texts into one key. So is text the system cannot map.
std::string upperCased(std::string text)
{
    const auto wide = eacp::Strings::widen(text);

    if (wide.empty() || eacp::Strings::narrow(wide) != text)
        return asciiUpperCased(std::move(text));

    const auto length = static_cast<int>(wide.size());
    auto upper = std::wstring(wide.size(), L'\0');

    if (LCMapStringEx(LOCALE_NAME_INVARIANT,
                      LCMAP_UPPERCASE,
                      wide.data(),
                      length,
                      upper.data(),
                      length,
                      nullptr,
                      nullptr,
                      0)
        != length)
        return asciiUpperCased(std::move(text));

    for (auto i = 0; i < length; ++i)
        if (isSurrogate(wide[static_cast<std::size_t>(i)]))
            upper[static_cast<std::size_t>(i)] = wide[static_cast<std::size_t>(i)];

    return eacp::Strings::narrow(upper);
}

// Repeated separators read as one. A UNC path's leading "//" is its root, not a
// repeat.
std::string withSingleSeparators(std::string_view text)
{
    auto result = std::string();
    result.reserve(text.size());

    const auto rootLength = text.starts_with("//") ? 2 : 0;
    result.append(text.substr(0, rootLength));

    for (const auto character: text.substr(rootLength))
        if (character != '/' || result.empty() || result.back() != '/')
            result += character;

    return result;
}

// "\\?\C:\x" names what "C:\x" names, and "\\?\UNC\server\share\x" what
// "\\server\share\x" does. Nothing else is a long-path prefix here: "\\?\" alone
// or "\\?\UNC" stays as it is. Takes upper-cased text with single '/'
// separators, and strips until no prefix is left, so a key's key is itself.
std::string withoutLongPathPrefix(std::string text)
{
    constexpr auto prefix = std::string_view("//?/");
    constexpr auto unc = std::string_view("//?/UNC/");

    for (;;)
    {
        if (text.starts_with(unc))
            text = "//" + text.substr(unc.size());
        else if (text.starts_with(prefix) && text.size() >= prefix.size() + 2
                 && text[prefix.size()] >= 'A' && text[prefix.size()] <= 'Z'
                 && text[prefix.size() + 1] == ':')
            text = text.substr(prefix.size());
        else
            return text;
    }
}

// A trailing separator reads as none, except where it is the root: the '/' of
// "C:/" or "/" ("C:" alone is that drive's current folder, somewhere else), or a
// bare "//".
std::string withoutTrailingSeparator(std::string text)
{
    const auto isRoot =
        text == "/" || text == "//" || (text.size() == 3 && text[1] == ':');

    if (!isRoot && text.size() > 1 && text.back() == '/')
        text.pop_back();

    return text;
}
} // namespace

std::string pathKey(std::string_view text)
{
    auto key = upperCased(eacp::FilePath::fromNative(text).str());
    key = withSingleSeparators(key);
    key = withoutLongPathPrefix(std::move(key));
    return withoutTrailingSeparator(std::move(key));
}

} // namespace MakeASound::Hosting
