#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace MakeASound
{

// What a parameter is beyond its range: every concrete parameter takes one of
// these last, so `{.sessionOnly = true}` or `{.hostId = 7}` reads at the
// declaration.
//
// Identity, before a plugin ships: the host id is FNV-1a of the parameter's id
// *path*, every enclosing named group's id in front ("Filter/Cutoff"; a '/'
// inside one id is doubled, see IdPath.h), and that path is also its key in
// saved state, one nested object per group. Renaming a parameter, or moving it into,
// out of or between groups, therefore changes both and breaks every session and
// automation lane that used it, unless `id` (for the name) or `hostId` (for the
// host id) is pinned first. Pin them before regrouping. A port's IdScheme
// derives the host id otherwise (a JUCE key's hash, or an index), but the state
// key is the path in every scheme.
struct ParameterOptions
{
    // The key in saved state and the source of the host id; the name when
    // empty, so pin one to rename a parameter without breaking either. A group
    // move still changes the path around it: pin `hostId` too for that.
    std::string_view id = {};
    std::string_view shortName = {};
    std::string_view label = {};
    bool automatable = true;
    bool bypass = false;

    // Written and read in a DAW session only, never in a preset.
    bool sessionOnly = false;

    // False keeps it out of saved state altogether: written in no context and
    // never read, so a load leaves it where it is even when the document holds
    // its key. Registered as any other parameter: ids, index and host
    // visibility are unchanged. For padding that only holds later ids in place.
    bool persistent = true;

    // Overrides the derived id (the path's hash, a JUCE key's hash or an
    // index), e.g. to keep the id a release that named it differently gave to
    // the host: the VST3 and AU id in every IdScheme. It moves no other entry's
    // index. Below 2^31, except under IdScheme::JUCE with juceStudioOneMask off
    // (ParameterList::identityProblems() lists it otherwise).
    std::optional<uint32_t> hostId = {};

    // Under a JUCE IdScheme, the leaf key the host ids come from: JUCE's
    // ParameterID::paramID. The id when empty, the leaf's own and not its path,
    // since JUCE hashes the leaf id; unique across the whole tree (asserted).
    // Native and TamberPlug ignore it.
    std::string_view hostKey = {};

    // Under a JUCE IdScheme, the AAX_CParamID in place of the hostKey: at most
    // 31 characters and unique after truncation to 31 (asserted), as JUCE
    // requires of every id. Native and TamberPlug ignore it.
    std::string_view aaxId = {};

    // The release that added the parameter, JUCE's ParameterID::versionHint.
    // Logic and GarageBand address an AU's parameters by their index in its
    // parameter list, which is ordered by (versionHint, host id): give every
    // parameter of the first release 1 and each later addition its release's
    // higher number, and an inserted parameter lands after all the old ones.
    // 0 is unset, which a debug AU build asserts on. Under an index profile
    // (JUCELegacyIndex, TamberPlug) a hint past the description's
    // overrides.indexedThroughVersion makes the parameter post-port: a path
    // hash for its ids, listed after every indexed one, moving no index.
    int versionHint = 0;
};

// One automatable value. The audio thread reads it with getValue() or the
// subclass's typed accessor; the host and the message thread write it. Every
// value member is a relaxed atomic, so neither side ever waits on the other.
class Parameter
{
public:
    enum class StateFormat
    {
        Number,
        Text,
        Bool
    };

    // The shape of toNormalized over the plain range, as far as a host can
    // draw it: an AU publishes the plain range with a display flag for each
    // named curve, and 0..1 for Other.
    enum class Curve
    {
        Linear,
        SquareRoot,
        Squared,
        Cubed,
        CubeRoot,
        Other
    };

    Parameter(std::string_view nameToUse, ParameterOptions options);
    virtual ~Parameter() = default;

    Parameter(const Parameter&) = delete;
    Parameter& operator=(const Parameter&) = delete;

    const std::string& name() const noexcept { return paramName; }
    const std::string& id() const noexcept { return paramId; }
    const std::string& shortName() const noexcept { return paramShortName; }
    const std::string& label() const noexcept { return paramLabel; }
    bool isAutomatable() const noexcept { return automatable; }
    bool isBypass() const noexcept { return bypass; }
    bool isSessionOnly() const noexcept { return sessionOnly; }
    bool isPersistent() const noexcept { return persistent; }
    std::optional<uint32_t> explicitHostId() const noexcept { return hostId; }
    int versionHint() const noexcept { return hint; }

    // The id when no hostKey was given.
    const std::string& hostKey() const noexcept { return paramHostKey; }

    // Empty when none was given.
    const std::string& explicitAAXId() const noexcept { return paramAAXId; }

    virtual float minValue() const noexcept = 0;
    virtual float maxValue() const noexcept = 0;
    virtual float defaultValue() const noexcept = 0;

    // 0 for a continuous parameter, the number of steps for a stepped one.
    virtual int numSteps() const noexcept { return 0; }

    // JUCE's isDiscrete() and isBoolean(): a choice and a bool are discrete, a
    // bool boolean, and an int or a stepped float neither, as in JUCE. Only the
    // JUCE-mode AU range (Compat::juceAUParameterInfo) and the JUCE-mode VST3 and
    // AAX steps (StepRule::JuceDiscrete) read them.
    virtual bool isDiscrete() const noexcept { return false; }
    virtual bool isBoolean() const noexcept { return false; }

    // A non-finite value is dropped and the parameter keeps what it held.
    virtual void setValue(float plain) noexcept = 0;
    virtual float getValue() const noexcept = 0;

    void resetToDefault() noexcept { setValue(defaultValue()); }

    void setNormalized(float normalized) noexcept;
    float getNormalized() const noexcept;

    // Linear over [minValue, maxValue], snapped to the step grid when stepped.
    // An override with another shape overrides curve() too.
    virtual float toNormalized(float plain) const noexcept;
    virtual float toPlain(float normalized) const noexcept;

    virtual Curve curve() const noexcept { return Curve::Linear; }

    // The host-facing readout and its inverse; text that holds no value maps
    // to the default.
    virtual std::string valueToText(float plain) const;
    virtual float textToValue(std::string_view text) const;

    // How saved state stores the value. Loading accepts every form whatever
    // this says, so a document written before a format changed still loads.
    virtual StateFormat stateFormat() const noexcept { return StateFormat::Number; }

    // A stable key for saved state: no unit, no display rounding.
    virtual std::string toStateText() const;

    // False when the text means nothing to this parameter, e.g. a choice
    // renamed since the document was written.
    virtual bool fromStateText(std::string_view text);

protected:
    // Renames the parameter in place, for one whose names come from elsewhere at
    // run time: a hosted plugin's parameter, renamed by the plugin it stands for.
    // The id, and so every host id and state key, stays as constructed. Message
    // thread; the owning list's display names follow on
    // ParameterList::refreshDisplayNames().
    void setDisplayInfo(std::string_view nameToUse,
                        std::string_view shortNameToUse,
                        std::string_view labelToUse);

    // JUCE's mapping for an integer or a choice, the plain values the integers
    // from minValue to maxValue: linear, the plain value rounded half to even
    // as juce::roundToInt does.
    float indexToNormalized(float plain) const noexcept;
    float indexToPlain(float normalized) const noexcept;
    static float roundToIndex(float plain) noexcept;

    static std::optional<float> parseNumber(std::string_view text);
    // value through a std::format-style brace pattern ("{:+.1f} dB"), checked at
    // run time: a bad pattern comes back as error text, never a throw.
    static std::string formatNumber(std::string_view pattern, float value);
    static std::string shortestText(float value);
    static std::string_view trim(std::string_view text) noexcept;
    static bool equalsIgnoringCase(std::string_view a, std::string_view b) noexcept;
    static bool containsIgnoringCase(std::string_view text,
                                     std::string_view part) noexcept;

private:
    std::string paramName;
    std::string paramId;
    std::string paramShortName;
    std::string paramLabel;
    bool automatable;
    bool bypass;
    bool sessionOnly;
    bool persistent;
    std::optional<uint32_t> hostId;
    int hint;
    std::string paramHostKey;
    std::string paramAAXId;
};

} // namespace MakeASound
