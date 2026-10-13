#pragma once

#include "JuceTree.h"
#include "../Parameters/ParameterGroup.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>

// A JUCE AudioProcessorValueTreeState's saved state applied to MakeASound
// parameters (juce_audio_processors/utilities/juce_AudioProcessorValueTreeState.cpp):
// a root whose type is the identifier the plugin gave its APVTS, and one direct
// child `PARAM id="<paramID>" value="<plain>"` per parameter, groups flattened.
// The values are plain: a float in its own units, a choice's index, a bool's 0
// or 1, an int; MakeASound's setValue() takes the same, so a parameter whose
// range matches its JUCE original (skew included) lands where it was.
namespace MakeASound::Compat
{

// What becomes of a parameter the state does not mention. JUCE's replaceState
// keeps its current value, which is the default here too.
enum class MissingPolicy
{
    Default,
    Keep
};

// The JUCE paramID a parameter was saved under, given the parameter and its id
// path ("Osc 1/Attack"). Unset, it is the parameter's own id (the leaf, not the
// path), which is what a port that kept its ids needs.
using JuceIdMap =
    std::function<std::string(const Parameter& param, std::string_view idPath)>;

struct ApvtsOptions
{
    // The root's type the plugin's APVTS was built with; any when empty. JUCE
    // plugins check it (`xml->hasTagName (state.state.getType())`) before
    // replaceState, and nothing is applied when it differs.
    std::string_view rootType = {};
    JuceIdMap mapId = {};
    MissingPolicy missing = MissingPolicy::Keep;
};

// Applies an APVTS tree to every parameter under `params`: a PARAM of the
// parameter's JUCE id sets its plain value (its `value` read as JUCE's var
// reads it; a PARAM with no `value` gives the default, as setNewState does),
// the last PARAM of an id winning; a parameter with none follows `missing`.
// False, and nothing touched, when the root's type is not `rootType`.
bool loadApvtsTree(const JuceTree& root,
                   ParameterGroup& params,
                   const ApvtsOptions& options = {});

// The same from XmlElement text. The parsed tree comes back, so the plugin can
// read whatever else it put beside the parameters; nullopt when the text is no
// XML or the root's type differs.
std::optional<JuceTree> loadApvtsXml(std::string_view xml,
                                     ParameterGroup& params,
                                     const ApvtsOptions& options = {});

// The same from the bytes a JUCE plugin's getStateInformation wrote: a
// copyXmlToBinary blob (the common `copyXmlToBinary (*state.copyState().createXml())`),
// otherwise a ValueTree::writeToStream (`state.copyState().writeToStream`).
std::optional<JuceTree> loadJuceApvtsBlob(std::string_view bytes,
                                          ParameterGroup& params,
                                          const ApvtsOptions& options = {});

} // namespace MakeASound::Compat
