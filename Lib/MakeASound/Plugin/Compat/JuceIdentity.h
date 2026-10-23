#pragma once

#include "../Core/Description.h"
#include "../Parameters/Parameter.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

// What a JUCE 8.0.12 build names its plugin by, and how its AU publishes a
// parameter, as pure functions, so a port keeps its users' sessions. Each is
// checked against values JUCE itself computed (Tests/JuceCompatTests.cpp). JUCE
// paths are relative to its modules/.
namespace MakeASound::Compat
{

// VST3Interface::jucePluginId for the component
// (juce_audio_processors_headless/utilities/juce_VST3Interface.h):
// FUID(0xABCDEF01, 0x9182FAEB, manufacturer, plugin).
Vst3ClassId juceVst3ClassId(FourCC manufacturer, FourCC plugin) noexcept;

// VST3Interface::vst2PluginId, the class id under JUCE_VST3_CAN_REPLACE_VST2:
// the bytes 'V' 'S' 'T', the VST2 unique id big-endian and up to nine of the
// name's bytes lowercased (ASCII, as std::tolower in the C locale), zero-padded.
// JUCE swaps bytes 0-3, 1-2, 4-5 and 6-7 on Windows, which is exactly the COM
// layout of the same four words, so the words are the same everywhere.
Vst3ClassId juceVst2ClassId(uint32_t vstUniqueId, std::string_view name) noexcept;

// The index of a main bus's channel set in JUCE's AAX list (juce_AAXClientExtensions
// .cpp's channelSets): 0 for no bus, 1 mono, 2 stereo. A bus of more channels is
// ambiguous by count alone (LCRS or quadraphonic), so -1.
int juceAAXChannelSetIndex(int channels) noexcept;

// AAXClientExtensions::getPluginIDForMainBusConfig: 'jcaa' ('jyaa' for
// AudioSuite), its third letter advanced through a-zA-Z0-9 by the input set's
// index and its fourth by the output's. A set index out of range (-1 included)
// leaves its letter alone, as JUCE does after its assert.
uint32_t juceAAXPluginId(int inputSetIndex,
                         int outputSetIndex,
                         bool audioSuite = false) noexcept;

// The AU and AUv3 parameter as JUCE publishes it (AU_1.mm GetParameterInfo,
// AUv3.mm createParameter): 0..1, or 0..steps for a discrete one, and every
// value a host sees the normalized one times the maximum. Every parameter is
// listed, a non-automatable one NonRealTime. A discrete parameter is a choice or
// a bool (Parameter::isDiscrete), never an int or a stepped float.
struct JuceAUParameterInfo
{
    float minValue = 0.f;
    float maxValue = 1.f;

    // The normalized default times the maximum. An AUv3 has no default; JUCE's
    // sets its tree's first value to the normalized default instead.
    float defaultValue = 0.f;

    // Unit Boolean for a boolean, Indexed for another discrete one, Generic
    // otherwise and under JUCE_FORCE_LEGACY_PARAMETER_AUTOMATION_TYPE.
    bool discrete = false;
    bool boolean = false;
    bool indexedUnit = false;

    // Flags beside readable, writable, HasCFNameString and ValuesHaveStrings,
    // which every parameter has.
    bool highResolution = true;
    bool canRamp = true;
    bool nonRealTime = false;

    // Whether the v2 unit answers ParameterValueStrings (discrete, and not under
    // JUCE_FORCE_USE_LEGACY_PARAM_IDS) and the AUv3 parameter carries
    // valueStrings (discrete, and not under the legacy automation type): one per
    // value 0..maxValue, the text of value / maxValue.
    bool v2ValueStrings = false;
    bool v3ValueStrings = false;

    float toHost(float normalized) const noexcept { return normalized * maxValue; }
    float toNormalized(float hostValue) const noexcept
    {
        return maxValue > 0.f ? hostValue / maxValue : 0.f;
    }
};

JuceAUParameterInfo juceAUParameterInfo(const Parameter& param,
                                        bool automatable,
                                        IdScheme scheme,
                                        const JuceSettings& settings) noexcept;

// AudioProcessor::getDefaultNumParameterSteps(), what JUCE's getNumSteps()
// answers for a continuous parameter.
constexpr int juceDefaultNumSteps = 0x7fffffff;

// JUCE's getNumSteps() for the parameter: its number of values, numSteps() + 1,
// for a stepped one (a choice's choices, a bool's 2, an int's values), and
// juceDefaultNumSteps for a continuous one. JUCE's AudioParameterFloat ignores
// its interval and answers juceDefaultNumSteps, so a stepped float differs here.
int juceNumSteps(const Parameter& param) noexcept;

// ParameterInfo::stepCount as JUCE's VST3 publishes it (juce_audio_plugin_client
// _VST3.cpp, Param::updateParameterInfo): 0 for a parameter that is not discrete
// (an int, a float), else getNumSteps() - 1, which is numSteps(). Under
// JUCE_FORCE_LEGACY_PARAMETER_AUTOMATION_TYPE every parameter's getNumSteps() - 1,
// 0 for a continuous one.
int juceVST3StepCount(const Parameter& param, bool legacyAutomationType) noexcept;

// The AAX parameter's steps as JUCE's AAX publishes them (juce_audio_plugin_client
// _AAX.cpp, addAudioProcessorParameters): SetNumberOfSteps(getNumSteps(), at most
// 2048) for every parameter, so 2048 for a continuous one, and Discrete only for
// a discrete parameter; under JUCE_FORCE_LEGACY_PARAMETER_AUTOMATION_TYPE,
// Discrete for one of at most 1000 steps.
struct JuceAAXSteps
{
    int numberOfSteps = 0;
    bool discrete = false;

    constexpr bool operator==(const JuceAAXSteps&) const = default;
};

// getSafeNumberOfParameterSteps's limit, for old Pro Tools control surfaces.
constexpr int juceAAXMaxSteps = 2048;

JuceAAXSteps juceAAXSteps(const Parameter& param,
                          bool legacyAutomationType) noexcept;

} // namespace MakeASound::Compat
