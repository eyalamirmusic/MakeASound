#pragma once

#include <cstdint>
#include <string>

namespace MakeASound
{

struct TrackColor
{
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 0;

    friend bool operator==(const TrackColor&, const TrackColor&) = default;
};

// Where in its channel's signal chain the host placed the plugin.
enum class PluginLocation
{
    Unknown,
    PreVolumeFader,
    PostVolumeFader,
    UsedAsPanner
};

// What the host says about the track the plugin sits on. Each format tells what
// it can: VST3 all of it (IInfoListener), AU, AUv3 and AAX the name alone (the
// context name, Pro Tools' track name), the standalone nothing. `isValid` stays
// false until the host first says anything, which many hosts never do.
struct TrackInfo
{
    bool isValid = false;

    // The track's display name, "Lead Vocal".
    std::string name;

    // A key that survives a rename or a reorder, for per-track state.
    std::string uid;

    // What `index` counts in ("Input", "Output", "Channel"), 1-based, and this
    // namespace's position among the host's, also 1-based.
    std::string indexNamespace;
    int index = 0;
    int namespaceOrder = 0;

    TrackColor color {};
    PluginLocation location = PluginLocation::Unknown;

    friend bool operator==(const TrackInfo&, const TrackInfo&) = default;
};

} // namespace MakeASound
