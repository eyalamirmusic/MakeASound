#include "CLAPStreams.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace MakeASound::Hosting
{

namespace
{
// A plugin writing more than a state could ever be is refused rather than grown
// into: past this, write() reports an error.
constexpr auto maxStateBytes =
    static_cast<size_t>(std::numeric_limits<int32_t>::max());
} // namespace

CLAPOutStream::CLAPOutStream()
{
    stream.ctx = this;
    stream.write = [](const clap_ostream* s, const void* buffer, uint64_t size)
    {
        auto& self = *static_cast<CLAPOutStream*>(s->ctx);

        if (size == 0)
            return int64_t {0};

        if (buffer == nullptr || size > maxStateBytes - self.data.size())
            return int64_t {-1};

        try
        {
            self.data.append(static_cast<const char*>(buffer),
                             static_cast<size_t>(size));
        }
        catch (...)
        {
            return int64_t {-1};
        }

        return static_cast<int64_t>(size);
    };
}

CLAPInStream::CLAPInStream(std::string_view dataToUse)
    : data(dataToUse)
{
    stream.ctx = this;
    stream.read = [](const clap_istream* s, void* buffer, uint64_t size)
    {
        auto& self = *static_cast<CLAPInStream*>(s->ctx);

        if (buffer == nullptr && size > 0)
            return int64_t {-1};

        auto left = static_cast<uint64_t>(self.data.size() - self.position);
        auto count = static_cast<size_t>(std::min(size, left));

        if (count > 0)
            std::memcpy(buffer, self.data.data() + self.position, count);

        self.position += count;
        return static_cast<int64_t>(count);
    };
}

} // namespace MakeASound::Hosting
