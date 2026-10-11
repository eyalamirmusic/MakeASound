#pragma once

// The two plugins the wrapper suites drive: a gain effect over a StatePlugin and
// an instrument that echoes its MIDI. Each records what process() was handed into
// fixed storage, so the allocation suite can run them under the ban.

#include <MakeASound/Plugin/MakeASoundPlugin.h>

#include <array>
#include <functional>
#include <string>

namespace TestPlugins
{
using namespace MakeASound;

constexpr auto blockSize = 64;

using Block = std::array<float, blockSize>;

struct FilterParams : ParameterGroup
{
    FilterParams()
        : ParameterGroup("Filter")
    {
        add(cutoff);
    }

    HzParam cutoff {"Cutoff", 20.f, 20000.f, 1000.f};
};

struct GainParams : ParameterGroup
{
    GainParams() { add(gain, filter, mode, bypass, width); }

    DecibelParam gain {"Gain", -60.f, 12.f, 0.f};
    FilterParams filter;
    ChoiceParam mode {"Mode", {"Clean", "Warm", "Hot"}, 0};
    BoolParam bypass {"Bypass", false, {.bypass = true}};
    FloatParam width {"Width", 0.f, 1.f, 0.5f, {.sessionOnly = true}};
};

// A later release: `drive` inserted mid-list.
struct NewerGainParams : ParameterGroup
{
    NewerGainParams() { add(gain, drive, filter, mode, bypass, width); }

    DecibelParam gain {"Gain", -60.f, 12.f, 0.f};
    FloatParam drive {"Drive", 0.f, 1.f, 0.25f};
    FilterParams filter;
    ChoiceParam mode {"Mode", {"Clean", "Warm", "Hot"}, 0};
    BoolParam bypass {"Bypass", false, {.bypass = true}};
    FloatParam width {"Width", 0.f, 1.f, 0.5f, {.sessionOnly = true}};
};

template <typename ParamsT>
struct GainStateOf : State<ParamsT>
{
    void reflect(Miro::Reflector& ref) override
    {
        State<ParamsT>::reflect(ref);
        ref["preset"](preset);
    }

    std::string preset = "Init";
};

// Main buses of matching width, one or two channels, plus an optional mono
// sidechain input.
template <typename ParamsT>
struct GainPluginOf : StatePlugin<GainStateOf<ParamsT>>
{
    GainPluginOf() { formatAtConstruction = this->format(); }

    std::string_view name() const override { return "Gain"; }

    BusLayout getBusLayout() const override { return BusLayout::stereoInOut(); }

    bool acceptsLayout(const BusLayout& proposed) const override
    {
        auto inputs = proposed.inputs.size();
        auto width = proposed.getMainInputChannels();

        return (inputs == 1 || (inputs == 2 && proposed.inputs[1].numChannels == 1))
               && proposed.outputs.size() == 1
               && proposed.getMainOutputChannels() == width && width >= 1
               && width <= 2 && proposed.midiInputs.empty()
               && proposed.midiOutputs.empty();
    }

    void prepare(const ProcessSpec& specToUse) override
    {
        spec = specToUse;
        formatAtPrepare = this->format();
    }

    void reset() noexcept override { ++resets; }

    void process(ProcessContext& context) noexcept override
    {
        ++processed;
        inputBuses = context.inputs.size();
        playhead = context.playhead;

        record(context.mainInput(), seenInput);
        record(context.mainOutput(), seenOutput);

        boundSamples = 0;

        for (const auto& buffer: context.inputs)
            boundSamples += buffer.getNumChannels() * buffer.getNumSamples();

        for (const auto& buffer: context.outputs)
            boundSamples += buffer.getNumChannels() * buffer.getNumSamples();

        auto gain = this->params.gain.gain();

        for (auto channel: context.mainOutput())
            for (auto& sample: channel)
                sample *= gain;
    }

    static void record(const Buffer& buffer, std::array<Block, 2>& into) noexcept
    {
        for (auto& block: into)
            block.fill(-1.f);

        for (auto ch = 0; ch < buffer.getNumChannels() && ch < 2; ++ch)
            for (auto s = 0; s < buffer.getNumSamples() && s < blockSize; ++s)
                into[ch][s] = buffer[ch][s];
    }

    PluginFormat formatAtConstruction = PluginFormat::Unknown;
    PluginFormat formatAtPrepare = PluginFormat::Unknown;
    ProcessSpec spec;
    Playhead playhead;
    int processed = 0;
    int resets = 0;
    int inputBuses = 0;

    // Channels times samples across every audio bus the block was handed.
    int boundSamples = -1;
    std::array<Block, 2> seenInput {};
    std::array<Block, 2> seenOutput {};
};

using GainPlugin = GainPluginOf<GainParams>;
using NewerGainPlugin = GainPluginOf<NewerGainParams>;

struct SynthParams : ParameterGroup
{
    SynthParams() { add(level); }

    FloatParam level {"Level", 0.f, 1.f, 0.5f};
};

// Writes `level` into every output sample and echoes each MIDI event it is
// handed to its MIDI output.
struct SynthPlugin : StatePlugin<State<SynthParams>>
{
    static constexpr int maxEvents = 16;

    std::string_view name() const override { return "Synth"; }

    BusLayout getBusLayout() const override
    {
        auto layout = BusLayout::instrument();
        layout.midiOutputs.add({"MIDI Out"});
        return layout;
    }

    void prepare(const ProcessSpec&) override {}

    void process(ProcessContext& context) noexcept override
    {
        midiOutWasEmpty = context.mainMidiOut().empty();
        numEvents = 0;

        for (const auto& event: context.mainMidiIn())
        {
            if (numEvents < maxEvents)
                events[numEvents++] = event;

            context.mainMidiOut().add(event);
        }

        context.mainOutput().fill(params.level.get());
    }

    bool midiOutWasEmpty = false;
    int numEvents = 0;
    std::array<MIDI::Event, maxEvents> events {};
};

// What the test module (describeModule() in PluginTests.cpp) creates. A suite
// hosting the module through a format's C API reaches the plugin behind a unit
// through `lastCreated`, and hosts another plugin under an entry's codes by
// setting that entry's stand-in.
namespace Module
{
inline Plugin* lastCreated = nullptr;
inline PluginCreateFn gainStandIn;
inline PluginCreateFn echoStandIn;

template <typename P>
OwningPointer<Plugin> create(const PluginCreateFn& standIn)
{
    auto plugin = standIn ? standIn() : OwningPointer<Plugin>(EA::makeOwned<P>());
    lastCreated = plugin.get();
    return plugin;
}

// Stands P in for an entry while it lives.
template <typename P>
struct ScopedStandIn
{
    explicit ScopedStandIn(PluginCreateFn& slotToUse)
        : slot(slotToUse)
    {
        slot = [] { return OwningPointer<Plugin>(EA::makeOwned<P>()); };
    }

    ~ScopedStandIn() { slot = nullptr; }

    ScopedStandIn(const ScopedStandIn&) = delete;
    ScopedStandIn& operator=(const ScopedStandIn&) = delete;

    PluginCreateFn& slot;
};
} // namespace Module

} // namespace TestPlugins
