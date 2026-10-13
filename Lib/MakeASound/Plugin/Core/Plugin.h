#pragma once

#include "../../Audio/Processor.h"
#include "BypassDelay.h"
#include "Editor.h"
#include "HostEditListener.h"
#include "TrackInfo.h"
#include "../Parameters/BoolParam.h"
#include "../Parameters/ParameterList.h"
#include "../State/StateContext.h"

#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace MakeASound
{

enum class PluginFormat
{
    Unknown,
    VST3,
    AU,
    AUv3,
    AAX,
    Standalone
};

// Which format of which framework wrote a state that is not ours.
enum class ForeignOrigin
{
    // A JUCE VST3's component state, its private-data trailer already stripped.
    JuceVST3,

    // A JUCE VST3 built with JUCE_VST3_CAN_REPLACE_VST2, or a VST2 session the
    // host converted: the chunk out of the 'FBCh' bank, trailer stripped. For a
    // bank of plain values ('FxBk'), the whole bank: Compat::readVst2Bank reads it.
    JuceVST2Bank,

    // The `jucePluginState` data of an AU v2 ClassInfo dictionary.
    JuceAU,

    // The `jucePluginState` data of an AUv3's fullState.
    JuceAUv3,

    // The 'juce' chunk of an AAX session.
    JuceAAX,

    // The `filterState` of a JUCE standalone's settings file, base64 decoded.
    JuceStandalone,

    // The `TamberPlugState` data of an AU v2 ClassInfo dictionary a Tamber Plug
    // build saved (AU/Adapter.cpp's SaveState), whichever unit it reaches: a
    // State document, `version` and a `params` object, written in a session. Its
    // schema is this framework's own (each leaf under its id, each child list as a
    // sub-object; a choice by name, a bool as 0 or 1), so a port that kept
    // Plug's ids and plain-value ranges reads it with
    // `state.deserialize(foreign.bytes, foreign.context)`. Plug's VST3 state is
    // the same document with no framing, and loads as a native one.
    TamberPlugAU
};

// A document another framework's build of this plugin saved, handed over by an
// adapter that found no MakeASound document. `bytes` are exactly what that
// build's AudioProcessor::setStateInformation was given, every format's framing
// taken off; they live only for the call.
struct ForeignState
{
    ForeignOrigin origin = ForeignOrigin::JuceVST3;
    std::string_view bytes;
    StateContext context = StateContext::Session;

    // The wrapper's bypass the format saved beside the plugin's bytes (the VST3
    // trailer's `Bypass`, the VST2 bank's 'VstW' flag), when it saved one.
    std::optional<bool> bypass;

    // The VST2 bank's current program, when there was a bank.
    std::optional<int> program;
};

// A Processor with a host behind it: a name, a layout it negotiates, latency and
// tail, a parameter registry, a state document and an optional editor.
class Plugin : public Processor
{
public:
    Plugin() = default;

    Plugin(const Plugin&) = delete;
    Plugin& operator=(const Plugin&) = delete;

    // Pure: a module may hold several plugin classes, so none of them can take
    // its name from describeModule(), and a placeholder would reach a window title.
    virtual std::string_view name() const = 0;

    // Set by the wrapper right after construction, so valid from prepare() on.
    PluginFormat format() const noexcept { return pluginFormat; }
    void setFormat(PluginFormat formatToUse) noexcept { pluginFormat = formatToUse; }

    // The default accepts the declared channel counts and nothing else.
    virtual bool acceptsLayout(const BusLayout& proposed) const;

    // At the prepared sample rate; hosts ask after prepare().
    virtual int latencySamples() const noexcept { return 0; }

    // How long the output rings after the input goes silent; 0 for none.
    virtual int tailSamples() const noexcept { return 0; }

    // Runs in place of process() while the bypass PluginWrapper adds for an effect
    // that declares none is on (a plugin with a bypass parameter of its own
    // bypasses itself, and this is never called for it). The default puts the
    // main input on the main output delayed by latencySamples(), so the bypassed
    // signal lines up with the processed one, copies each MIDI in bus to the MIDI
    // out bus of the same index, and clears every other output. Audio thread.
    virtual void processBypassed(ProcessContext& context) noexcept;

    // True once each time latencySamples() moved on its own since the last ask.
    // Message thread.
    virtual bool takeLatencyChanged() noexcept { return false; }

    // Null means no editor of its own, and the host shows its generic parameter
    // page. Cheap by contract: a host may call it only to learn whether one
    // exists, so build the view lazily in Editor::view(). Message thread.
    virtual OwningPointer<Editor> createEditor() { return {}; }

    // The plugin's whole document, on the message thread.
    virtual std::string saveState(StateContext context);
    virtual void loadState(std::string_view data, StateContext context);

    // The same document from any thread, or empty when the plugin cannot make
    // one there, in which case the wrapper marshals to the message thread.
    virtual std::string saveStateWithoutMessageThread(StateContext context);

    // False when the snapshot above has fallen behind the plugin.
    virtual bool isStateSnapshotCurrent() const { return true; }

    // The parameters out of loadState's document and nothing else. They are
    // atomic, so the wrapper applies them on the host's thread.
    virtual void loadParameters(std::string_view data, StateContext context);

    // Everything loadParameters() leaves, on the message thread. The default
    // loads the whole document, for a plugin that cannot split it.
    virtual void loadStateExceptParameters(std::string_view data,
                                           StateContext context);

    // A state another framework wrote (a JUCE build's session, say), which the
    // adapters hand over when the host's data holds no MakeASound document. True
    // when the plugin took it; the default takes nothing. Parameters and the rest
    // alike, on the message thread: Compat/ has the readers.
    virtual bool loadForeignState(const ForeignState& state);

    const ParameterList& parameters() const noexcept { return parameterList; }

    // The last the host said about the track (see TrackInfo); not valid until it
    // says something. Message thread.
    const TrackInfo& trackInfo() const noexcept { return currentTrackInfo; }

    // The host said something about the track, already in trackInfo(). Each time
    // it tells, changed or not. Message thread.
    virtual void onTrackInfoChanged(const TrackInfo& info);

    // Message thread; null where there is no host.
    void setHostEditListener(HostEditListener* listener) noexcept
    {
        editListener = listener;
    }

    HostEditListener* hostEditListener() const noexcept { return editListener; }

    void notifyHostLatencyChanged() noexcept;
    void notifyHostParameterInfoChanged() noexcept;

    // Message thread. Many values moved at once: the host re-reads them all.
    void notifyHostParameterValuesChanged() noexcept;

    // Message thread. State besides the parameters changed; StatePlugin raises
    // it on every markChanged() that is not a load.
    void notifyHostStateChanged() noexcept;

protected:
    // Before the wrapper is built: it sizes itself to the list. A list set again
    // later gets the wrapper's bypass back unless it brings a bypass of its own.
    void setParameters(ParameterList list);

    // After renaming parameters in place (Parameter::setDisplayInfo): the list's
    // display names follow, and the host is told when one changed. Ids stay.
    // Message thread.
    void refreshParameterNames();

private:
    friend class PluginWrapper;

    // The wrapper's half of the added bypass: it decides, at construction, and
    // sizes the delay in prepare(). Null when the plugin has a bypass of its own.
    void addHostBypass();
    BoolParam* activeHostBypass() const noexcept;

    // Message thread: grows the added bypass's delay to latencySamples().
    void fitBypassDelay() noexcept;

    // The wrapper's, on the message thread: keeps it, then onTrackInfoChanged().
    void setTrackInfo(TrackInfo info);
    TrackInfo currentTrackInfo;

    // The wrapper's, after the bypass: every host id under the plugin's
    // description's scheme, kept for a list set again later.
    void setParameterIdentity(ParameterIdentity identity);
    std::optional<ParameterIdentity> parameterIdentity;

    // Before the list, which points at it.
    OwningPointer<BoolParam> hostBypass;
    ParameterList parameterList;
    BypassDelay bypassDelay;
    PluginFormat pluginFormat = PluginFormat::Unknown;
    HostEditListener* editListener = nullptr;
};

} // namespace MakeASound
