#pragma once

#include "../State/StateContext.h"

#include <clap/ext/state-context.h>
#include <clap/ext/state.h>
#include <clap/stream.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace MakeASound::CLAP
{

// Until a read returns 0 (the end) or less (an error, which yields nullopt), in
// 4 KiB reads. Nullopt for a stream past 2^31 - 1 bytes too.
std::optional<std::string> readAll(const clap_istream& stream);

// Every byte, through as many writes as the host takes; false on an error or a
// write that takes nothing.
bool writeAll(const clap_ostream& stream, std::string_view data) noexcept;

// CLAP_STATE_CONTEXT_FOR_PRESET is a Preset; FOR_PROJECT and FOR_DUPLICATE a
// Session; anything else nullopt.
std::optional<StateContext> stateContextFor(uint32_t contextType) noexcept;

// clap.state (always a Session) and clap.state-context, over the Adapter behind
// the plugin.
const clap_plugin_state* stateExtension() noexcept;
const clap_plugin_state_context* stateContextExtension() noexcept;

} // namespace MakeASound::CLAP
