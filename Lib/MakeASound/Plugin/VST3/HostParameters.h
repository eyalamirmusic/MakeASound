#pragma once

#include "VST3Common.h"
#include "../Parameters/ParameterList.h"

#include <cstdint>
#include <optional>
#include <string>

namespace MakeASound::VST3
{

// The hidden parameters a host maps MIDI controllers onto, IMidiMapping's only
// route for CC, pitch bend, aftertouch and program change into a VST3 plugin.
// They take the top 2^16 ids of the 31-bit space VST3 leaves to plugins.
inline constexpr uint32_t shadowIdBase = 0x7fff0000u;
inline constexpr int maxShadowBuses = 16;

// 0..127, kAfterTouch, kPitchBend, kCtrlProgramChange.
inline constexpr int shadowControllers = 131;

struct ShadowTarget
{
    int bus = 0;
    int channel = 0;
    int controller = 0;
};

constexpr Vst::ParamID shadowIdFor(int bus, int channel, int controller) noexcept
{
    return shadowIdBase | (static_cast<uint32_t>(bus & 0xf) << 12)
           | (static_cast<uint32_t>(channel & 0xf) << 8)
           | static_cast<uint32_t>(controller & 0xff);
}

// nullopt unless id is in the range, controller <= 130 and bus < numMidiInputs.
std::optional<ShadowTarget> decodeShadowId(Vst::ParamID id,
                                           int numMidiInputs) noexcept;

// "MIDI CC 74 Ch 1", "Aftertouch Ch 1", "Pitch Bend Ch 1", "Program Change Ch 1",
// with " Bus 2" appended only when there is more than one MIDI input bus.
std::string shadowName(const ShadowTarget& target, int numMidiInputs);

// The host's view of one Parameter, which it never owns: every conversion goes
// through the MakeASound parameter, so a skewed range shows as it should.
class ProxyParameter : public Vst::Parameter
{
public:
    explicit ProxyParameter(const ParameterList::Entry& entryToUse);

    // Re-reads title, units, steps and default for parameterInfoChanged().
    void refreshInfo();

    void toString(Vst::ParamValue normalized, Vst::String128 text) const override;
    bool fromString(const Vst::TChar* text,
                    Vst::ParamValue& normalized) const override;
    Vst::ParamValue toPlain(Vst::ParamValue normalized) const override;
    Vst::ParamValue toNormalized(Vst::ParamValue plain) const override;

private:
    const MakeASound::Parameter& param() const noexcept { return *entry.param; }

    // The list outlives the controller.
    const ParameterList::Entry& entry;
};

} // namespace MakeASound::VST3
