#pragma once

#include "VST3Common.h"
#include "../Parameters/ParameterList.h"

#include <cstdint>
#include <optional>
#include <string>

namespace MakeASound::VST3
{

// The hidden parameters a host maps MIDI controllers onto, IMidiMapping's only
// route for CC, pitch bend, aftertouch and program change into a VST3 plugin,
// registered only for a plugin whose description asks for them
// (vst3MidiControllersFor()), laid out by its policy (Vst3ControllerLayout).
// MakeASound's take the top 2^16 ids of the 31-bit space VST3 leaves to plugins;
// a JUCE port's are JUCE's. A plugin parameter with an id in the range wins over
// the shadow (a JUCE port's String::hashCode may land there, masked or not).
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

// JUCE's layout has no program change: CC 0-127, aftertouch, pitch bend.
inline constexpr int juceShadowControllers = 130;

// Controllers per channel under the layout.
constexpr int shadowControllersFor(const Vst3Controllers& controllers) noexcept
{
    return controllers.layout == Vst3ControllerLayout::JuceContiguous
               ? juceShadowControllers
               : shadowControllers;
}

// The parameter id under the layout (Vst3ControllerLayout). JuceContiguous
// ignores the bus, as JUCE does.
constexpr Vst::ParamID shadowIdFor(const Vst3Controllers& controllers,
                                   int bus,
                                   int channel,
                                   int controller) noexcept
{
    if (controllers.layout == Vst3ControllerLayout::JuceContiguous)
        return controllers.base
               + static_cast<uint32_t>(channel) * juceShadowControllers
               + static_cast<uint32_t>(controller);

    return controllers.base | (static_cast<uint32_t>(bus & 0xf) << 12)
           | (static_cast<uint32_t>(channel & 0xf) << 8)
           | static_cast<uint32_t>(controller & 0xff);
}

// MakeASound's own layout, at shadowIdBase.
constexpr Vst::ParamID shadowIdFor(int bus, int channel, int controller) noexcept
{
    return shadowIdFor(Vst3Controllers {}, bus, channel, controller);
}

// nullopt unless id is in the layout's range, the controller is one it has and
// bus < numMidiInputs.
std::optional<ShadowTarget> decodeShadowId(const Vst3Controllers& controllers,
                                           Vst::ParamID id,
                                           int numMidiInputs) noexcept;

// MakeASound's own layout, at shadowIdBase.
std::optional<ShadowTarget> decodeShadowId(Vst::ParamID id,
                                           int numMidiInputs) noexcept;

// "MIDI CC 74 Ch 1", "Aftertouch Ch 1", "Pitch Bend Ch 1", "Program Change Ch 1",
// with " Bus 2" appended only when there is more than one MIDI input bus.
std::string shadowName(const ShadowTarget& target, int numMidiInputs);

// The title under the layout: shadowName, or JUCE's "MIDI CC 0|74".
std::string shadowName(const Vst3Controllers& controllers,
                       const ShadowTarget& target,
                       int numMidiInputs);

// The host's view of one Parameter, which it never owns: every conversion goes
// through the MakeASound parameter, so a skewed range shows as it should.
class ProxyParameter : public Vst::Parameter
{
public:
    // `unitToUse` is the unit the parameter is published in (unitIdFor()).
    explicit ProxyParameter(const ParameterList::Entry& entryToUse,
                            Vst::UnitID unitToUse = Vst::kRootUnitId);

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
    Vst::UnitID unit = Vst::kRootUnitId;
};

// The unit an entry is published in: its innermost group's
// (ParameterList::Group::unitId) where the identity's hostGroups publishes
// groups, the root unit otherwise.
Vst::UnitID unitIdFor(const ParameterList& list,
                      const ParameterList::Entry& entry) noexcept;

// IUnitInfo's units where the identity's hostGroups publishes groups, in
// order: the root unit ("Root Unit", as JUCE's), then each group with a
// registered entry below it, in groups() order, with its parent's unit (the
// root unit at the top level) and its own name, as JUCE's VST3 wrapper's.
// None otherwise, as before units existed.
struct UnitDescription
{
    Vst::UnitID id = Vst::kRootUnitId;
    Vst::UnitID parent = Vst::kNoParentUnitId;
    std::string name;
};

Vector<UnitDescription> unitsFor(const ParameterList& list);

} // namespace MakeASound::VST3
