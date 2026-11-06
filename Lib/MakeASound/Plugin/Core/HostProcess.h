#pragma once

#include <string>

namespace MakeASound
{

// The file name of the executable this code runs in, without its extension
// ("Live", "Ableton Live 12 Suite", "REAPER"), empty when the platform will not
// say. It is what JUCE's PluginHostType matches on, and the fallback where
// Plugin::hostName() is empty: under AU, which has no API naming its host, and
// in a constructor, before any adapter has said anything. It names the process,
// not the host: an AU that Logic runs out of process, or an AUv3 extension,
// sees its hosting service's or its own extension's name. Any thread.
std::string hostProcessName();

} // namespace MakeASound
