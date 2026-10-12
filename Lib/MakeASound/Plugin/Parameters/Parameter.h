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
struct ParameterOptions
{
    // The key in saved state and the source of the host id; the name when
    // empty, so pin one to rename a parameter without breaking either.
    std::string_view id = {};
    std::string_view shortName = {};
    std::string_view label = {};
    bool automatable = true;
    bool bypass = false;

    // Written and read in a DAW session only, never in a preset.
    bool sessionOnly = false;

    // Overrides the hash of the parameter's id, e.g. to keep the id a release
    // that named it differently gave to the host.
    std::optional<uint32_t> hostId = {};
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
    std::optional<uint32_t> explicitHostId() const noexcept { return hostId; }

    virtual float minValue() const noexcept = 0;
    virtual float maxValue() const noexcept = 0;
    virtual float defaultValue() const noexcept = 0;

    // 0 for a continuous parameter, the number of steps for a stepped one.
    virtual int numSteps() const noexcept { return 0; }

    // A non-finite value is dropped and the parameter keeps what it held.
    virtual void setValue(float plain) noexcept = 0;
    virtual float getValue() const noexcept = 0;

    void resetToDefault() noexcept { setValue(defaultValue()); }

    void setNormalized(float normalized) noexcept;
    float getNormalized() const noexcept;

    // Linear over [minValue, maxValue], snapped to the step grid when stepped.
    virtual float toNormalized(float plain) const noexcept;
    virtual float toPlain(float normalized) const noexcept;

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
    static std::optional<float> parseNumber(std::string_view text);
    static std::string formatNumber(const char* format, float value);
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
    std::optional<uint32_t> hostId;
};

} // namespace MakeASound
