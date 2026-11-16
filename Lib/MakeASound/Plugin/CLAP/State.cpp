#include "State.h"
#include "Adapter.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <limits>

namespace MakeASound::CLAP
{

namespace
{
constexpr auto chunkSize = 4096;

bool save(const clap_plugin* plugin, const clap_ostream* stream) noexcept
{
    return stream != nullptr
           && Adapter::from(plugin)->saveState(*stream, StateContext::Session);
}

bool load(const clap_plugin* plugin, const clap_istream* stream) noexcept
{
    return stream != nullptr
           && Adapter::from(plugin)->loadState(*stream, StateContext::Session);
}

bool saveIn(const clap_plugin* plugin,
            const clap_ostream* stream,
            uint32_t contextType) noexcept
{
    auto context = stateContextFor(contextType);
    return stream != nullptr && context
           && Adapter::from(plugin)->saveState(*stream, *context);
}

bool loadIn(const clap_plugin* plugin,
            const clap_istream* stream,
            uint32_t contextType) noexcept
{
    auto context = stateContextFor(contextType);
    return stream != nullptr && context
           && Adapter::from(plugin)->loadState(*stream, *context);
}

constexpr auto state = clap_plugin_state {save, load};
constexpr auto stateContext = clap_plugin_state_context {saveIn, loadIn};
} // namespace

std::optional<std::string> readAll(const clap_istream& stream)
{
    auto data = std::string {};
    auto chunk = std::array<char, chunkSize> {};

    while (true)
    {
        auto read = stream.read(&stream, chunk.data(), chunk.size());

        if (read < 0)
            return std::nullopt;

        if (read == 0)
            return data;

        auto count = std::min(static_cast<uint64_t>(read), uint64_t {chunk.size()});

        if (data.size() + count
            > static_cast<size_t>(std::numeric_limits<int32_t>::max()))
            return std::nullopt;

        data.append(chunk.data(), static_cast<size_t>(count));
    }
}

bool writeAll(const clap_ostream& stream, std::string_view data) noexcept
{
    auto written = size_t {0};

    while (written < data.size())
    {
        auto result =
            stream.write(&stream, data.data() + written, data.size() - written);

        if (result <= 0)
            return false;

        written += static_cast<size_t>(result);
    }

    return true;
}

std::optional<StateContext> stateContextFor(uint32_t contextType) noexcept
{
    switch (contextType)
    {
        case CLAP_STATE_CONTEXT_FOR_PRESET:
            return StateContext::Preset;

        case CLAP_STATE_CONTEXT_FOR_PROJECT:
        case CLAP_STATE_CONTEXT_FOR_DUPLICATE:
            return StateContext::Session;

        default:
            return std::nullopt;
    }
}

const clap_plugin_state* stateExtension() noexcept
{
    return &state;
}

const clap_plugin_state_context* stateContextExtension() noexcept
{
    return &stateContext;
}

bool Adapter::saveState(const clap_ostream& stream, StateContext context) noexcept
{
    assert(isMainThread());

    try
    {
        auto document = pluginWrapper.saveState(context);
        return writeAll(stream, document);
    }
    catch (...)
    {
        return false;
    }
}

bool Adapter::loadState(const clap_istream& stream, StateContext context) noexcept
{
    assert(isMainThread());

    try
    {
        auto document = readAll(stream);

        if (!document)
            return false;

        pluginWrapper.loadState(*document, context);
    }
    catch (...)
    {
        return false;
    }

    // The host re-reads every value, so the next block sends none of them.
    takeAllAsBaseline();

    if (hostParams != nullptr)
        hostParams->rescan(host, CLAP_PARAM_RESCAN_VALUES);

    checkLatency();
    return true;
}

} // namespace MakeASound::CLAP
