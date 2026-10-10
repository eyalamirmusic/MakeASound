#include "VST3Common.h"
#include "Stream.h"
#include "Text.h"

#include "pluginterfaces/base/funknownimpl.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstattributes.h"
#include "pluginterfaces/vst/vstpresetkeys.h"

#include <array>

namespace MakeASound::VST3
{

std::string readAll(IBStream& stream)
{
    auto data = std::string {};
    auto chunk = std::array<char, 4096> {};

    while (true)
    {
        auto bytesRead = int32 {0};
        auto result =
            stream.read(chunk.data(), static_cast<int32>(chunk.size()), &bytesRead);

        if (result != Steinberg::kResultOk || bytesRead <= 0)
            break;

        data.append(chunk.data(), static_cast<size_t>(bytesRead));
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
