#include "IdPath.h"

namespace MakeASound
{

namespace
{
constexpr auto separator = '/';
}

std::string escapeIdSegment(std::string_view id)
{
    auto segment = std::string {};
    segment.reserve(id.size());

    for (auto c: id)
    {
        segment += c;

        if (c == separator)
            segment += separator;
    }

    return segment;
}

void appendIdSegment(std::string& path, std::string_view id)
{
    if (!path.empty() && !id.empty())
        path += separator;

    path += escapeIdSegment(id);
}

Vector<std::string> splitIdPath(std::string_view path)
{
    auto ids = Vector<std::string> {};

    if (path.empty())
        return ids;

    auto current = std::string {};
    auto i = size_t {0};

    while (i < path.size())
    {
        if (path[i] != separator)
        {
            current += path[i++];
            continue;
        }

        auto run = size_t {0};

        while (i < path.size() && path[i] == separator)
        {
            ++run;
            ++i;
        }

        // An even run is that many halved inside one id; an odd one starts
        // with the separator, the rest opening the next id.
        if (run % 2 == 1)
        {
            ids.add(std::move(current));
            current.clear();
        }

        current.append(run / 2, separator);
    }

    ids.add(std::move(current));
    return ids;
}

bool isDelimitableId(std::string_view id) noexcept
{
    return id.empty() || (id.front() != separator && id.back() != separator);
}

} // namespace MakeASound
