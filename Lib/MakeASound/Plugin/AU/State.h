#pragma once

#include <CoreFoundation/CoreFoundation.h>

#include <optional>
#include <string>
#include <string_view>

namespace MakeASound::AU
{

// The plugin's document rides in the SDK's ClassInfo dictionary under one key of
// its own, beside the codes and parameter data the SDK writes and checks.
void writeState(CFPropertyListRef classInfo, std::string_view document);

// Nullopt when the dictionary carries no document of ours.
std::optional<std::string> readState(CFPropertyListRef classInfo);

} // namespace MakeASound::AU
