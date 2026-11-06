#pragma once

#include <algorithm>
#include <cctype>
#include <string_view>

namespace MakeASound::Hosting::Text
{

// ASCII case folding, which is all the comparisons below need: plugin names,
// categories and file extensions are matched the way a user types them.
inline bool equalIgnoringCase(char a, char b) noexcept
{
    return std::tolower(static_cast<unsigned char>(a))
           == std::tolower(static_cast<unsigned char>(b));
}

inline bool containsIgnoringCase(std::string_view text, std::string_view needle)
{
    if (needle.empty())
        return true;

    const auto found = std::search(
        text.begin(), text.end(), needle.begin(), needle.end(), equalIgnoringCase);

    return found != text.end();
}

inline bool endsWithIgnoringCase(std::string_view text, std::string_view suffix)
{
    if (suffix.size() > text.size())
        return false;

    const auto tail = text.substr(text.size() - suffix.size());

    return std::equal(tail.begin(), tail.end(), suffix.begin(), equalIgnoringCase);
}

} // namespace MakeASound::Hosting::Text
