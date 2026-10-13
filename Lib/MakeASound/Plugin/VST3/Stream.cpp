#include "VST3Common.h"
#include "Stream.h"
#include "Text.h"

#include "pluginterfaces/base/funknownimpl.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstattributes.h"
#include "pluginterfaces/vst/vstpresetkeys.h"

#include <algorithm>
#include <array>

namespace MakeASound::VST3
{

namespace
{
// A stream longer than this is junk, as JUCE judges it too.
constexpr auto maxStateBytes = size_t {0x7fffffff};

// Some hosts report junk for the size, and Cubase 9 a wrong one: it is only ever
// a reservation, and only below this.
constexpr auto maxSizeHint = Steinberg::int64 {100 * 1024 * 1024};

void reserveForStreamSize(IBStream& stream, std::string& data)
{
    auto sizeable = Steinberg::U::cast<Steinberg::ISizeableStream>(&stream);

    if (!sizeable)
        return;

    auto size = Steinberg::int64 {0};

    if (sizeable->getStreamSize(size) == Steinberg::kResultOk && size > 0
        && size < maxSizeHint)
        data.reserve(static_cast<size_t>(size));
}
} // namespace

std::string readAll(IBStream& stream)
{
    auto data = std::string {};
    reserveForStreamSize(stream, data);

    auto chunk = std::array<char, 4096> {};

    while (true)
    {
        auto bytesRead = int32 {0};
        auto result =
            stream.read(chunk.data(), static_cast<int32>(chunk.size()), &bytesRead);

        if (bytesRead <= 0)
            break;

        auto taken = std::min(static_cast<size_t>(bytesRead), chunk.size());

        if (data.size() + taken > maxStateBytes)
            return {};

        data.append(chunk.data(), taken);

        // Wavelab answers a read that delivered with a status other than
        // kResultOk, so the bytes count; a short one is then the end.
        if (result != Steinberg::kResultOk && taken < chunk.size())
            break;
    }

    return data;
}

bool writeAll(IBStream& stream, std::string_view data)
{
    while (!data.empty())
    {
        auto bytesWritten = int32 {0};
        auto* bytes = const_cast<char*>(data.data());
        auto result =
            stream.write(bytes, static_cast<int32>(data.size()), &bytesWritten);

        if (result != Steinberg::kResultOk || bytesWritten <= 0)
            return false;

        data.remove_prefix(static_cast<size_t>(bytesWritten));
    }

    return true;
}

StateContext stateContextOf(IBStream* stream)
{
    auto attributes = Steinberg::U::cast<Vst::IStreamAttributes>(stream);

    if (!attributes)
        return StateContext::Session;

    auto* list = attributes->getAttributes();

    if (list == nullptr)
        return StateContext::Session;

    Vst::String128 type {};

    if (list->getString(Vst::PresetAttributes::kStateType, type, sizeof(type))
        != Steinberg::kResultTrue)
        return StateContext::Session;

    return toUtf8(type) == Vst::StateType::kProject ? StateContext::Session
                                                    : StateContext::Preset;
}

} // namespace MakeASound::VST3
