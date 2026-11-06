#pragma once

#include "AAXCommon.h"

#include <string_view>

namespace MakeASound::AAX
{

// The one chunk: the wrapper's Session document.
constexpr AAX_CTypeID chunkId = ('M' << 24) | ('k' << 16) | ('a' << 8) | 'S';

// Two chunks a JUCE build of the plugin left in a session, read but never
// listed or written: its plugin state (JucePlugin_AAX_Chunk_Identifier's
// default) and the SDK's controls chunk, normalized values by AAX parameter id.
constexpr AAX_CTypeID juceChunkId = ('j' << 24) | ('u' << 16) | ('c' << 8) | 'e';
constexpr AAX_CTypeID controlsChunkId = ('e' << 24) | ('l' << 16) | ('c' << 8) | 'k';

// The document into the host's chunk, whose fSize says what it holds; false when
// it does not fit.
bool writeChunk(AAX_SPlugInChunk& chunk, std::string_view document) noexcept;

std::string_view readChunk(const AAX_SPlugInChunk& chunk) noexcept;

// True when both documents hold the same parameter values, the one at `bypassId`
// (an id path, "Group/Name", read by splitIdPath) left out, as the SDK leaves the master bypass out of
// its own compare.
bool sameParameters(std::string_view document,
                    std::string_view other,
                    std::string_view bypassId);

} // namespace MakeASound::AAX
