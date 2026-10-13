#pragma once

#include "../Core/Description.h"
#include "../Parameters/ParameterList.h"

#include <AudioToolbox/AudioToolbox.h>

#include <string>
#include <string_view>

namespace MakeASound::AU
{

// Every Copy/make below returns a +1 reference the caller releases.
CFStringRef makeCFString(std::string_view text);
std::string toUtf8(CFStringRef text);

// The host's view of one entry: its name, range and unit, IsHighResolution,
// ValuesHaveStrings only where hasValueStrings answers, and the display flag of
// its curve. The range is the plain one where the curve is linear or one a
// display flag draws exactly, and 0..1 otherwise (publishesNormalized), so a
// host lane over a skewed range moves as the plugin's own controls do.
void fillParameterInfo(const ParameterList::Entry& entry,
                       AudioUnitParameterInfo& info);

// Every value an AU host reads or writes (Globals(), SetParameter, the AUv3
// tree, the text properties) is in this space: plain, or normalized where
// publishesNormalized says so.
bool publishesNormalized(const Parameter& param) noexcept;
float toHostValue(const Parameter& param, float plain) noexcept;
float fromHostValue(const Parameter& param, float hostValue) noexcept;

// GetParameterList's order: the exposed entries' indices sorted by host id and
// then, stably, by versionHint, as JUCE's AU does, so a parameter added with a
// higher hint never moves an older one's index, which Logic automates by.
Vector<int> parameterListOrder(const ParameterList& list);

// The first exposed entry other than `skip` with no versionHint, -1 when none.
int firstUnhintedParameter(const ParameterList& list, int skip) noexcept;

// A choice's names or a bool's two states, one per step; false for the rest.
bool hasValueStrings(const Parameter& param) noexcept;
CFArrayRef copyValueStrings(const Parameter& param);

CFStringRef copyTextForValue(const Parameter& param, float plain);

// Clamped to the range; the current value when the text means no number.
float valueForText(const Parameter& param, CFStringRef text);

// Which AU format asks: the v2 unit or the AUv3 tree, whose JUCE value strings
// follow different settings.
enum class AUFlavour
{
    V2,
    V3
};

// How an AU host, v2 or AUv3, sees a plugin's parameters, by its description's
// IdScheme. MakeASound's scheme is everything above: the exposed entries, the
// plain or normalized range, fillParameterInfo. A JUCE scheme is JUCE's AU
// (Compat::juceAUParameterInfo): every entry listedInAU (a non-automatable one
// NonRealTime, the added bypass left to the bypass property), 0..1 or 0..steps
// for a choice and a bool, every value the normalized one times the maximum.
// Pure and allocation-free but for the info and the value strings.
class ParameterSpace
{
public:
    ParameterSpace() = default;
    ParameterSpace(IdScheme scheme, const JuceSettings& juce) noexcept;
    explicit ParameterSpace(const PluginDescription& plugin) noexcept;

    IdScheme scheme() const noexcept { return idScheme; }
    bool isJuce() const noexcept { return isJuceScheme(idScheme); }

    // Whether the host addresses the entry at all: isHostExposed under
    // MakeASound's scheme, listedInAU under a JUCE one.
    bool isListed(const ParameterList& list, int index) const noexcept;

    // The listed entries' indices in GetParameterList's order (see
    // parameterListOrder), and the first listed one other than `skip` without a
    // versionHint, -1 when none.
    Vector<int> listOrder(const ParameterList& list) const;
    int firstUnhinted(const ParameterList& list, int skip) const noexcept;

    void fillInfo(const ParameterList::Entry& entry,
                  AudioUnitParameterInfo& info) const;

    // A plain value in the host's space and back.
    float toHost(const ParameterList::Entry& entry, float plain) const noexcept;
    float fromHost(const ParameterList::Entry& entry,
                   float hostValue) const noexcept;

    bool hasValueStrings(const ParameterList::Entry& entry,
                         AUFlavour flavour) const noexcept;

    // Null when hasValueStrings says none.
    CFArrayRef copyValueStrings(const ParameterList::Entry& entry,
                                AUFlavour flavour) const;

private:
    IdScheme idScheme = IdScheme::MakeASound;
    JuceSettings juceSettings = {};
};

} // namespace MakeASound::AU
