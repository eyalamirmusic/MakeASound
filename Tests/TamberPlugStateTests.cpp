// A nested Tamber Plug parameter tree ported onto ParameterGroup keeps what
// Plug's own code made of it: the ids (under IdScheme::TamberPlug, the flat leaf
// index, hidden and session-only leaves counted) and the saved document (Miro
// JSON, each mounted list a sub-object under its prefix, choices by name).
//
// The goldens are Plug's: a throwaway program built against Plug's
// Lib/TamberPlug (makeasound-migration, its ParamList.cpp unchanged since
// 2457f60) walked Instruments::VirtualAnalogParams and a list with a hidden
// subtree and a session parameter through forEachLeaf, whose order is the
// ParamID Plug's VST3 and AU wrappers publish, and printed
// State::serialize(StateContext::Session) after the values below were set. The
// same program fed the ports' documents back to Plug's State, which loaded them
// to the values it saves itself: Plug reads a JSON bool as its 0 or 1 and skips
// the wrapper's top-level `__bypass`.

#include "TamberPlugFixtures.h"

#include <MakeASound/Plugin/MakeASoundPlugin.h>

#include <Miro/Json.h>
#include <NanoTest/NanoTest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

using namespace nano;
using namespace MakeASound;

namespace
{
constexpr auto v1 = ParameterOptions {.versionHint = 1};

// --- VirtualAnalog, as Plug's Instruments/VirtualAnalogParams.h declares it --

// Plug's CentsParam, DetuneParam, FrequencyParam and LfoRateParam are a plain
// FloatParam with a readout; FrequencyParam and LfoRateParam store a 0..1
// position, so the port's are FloatParams over 0..1 too.
struct VAOscParams : ParameterGroup
{
    VAOscParams(int waveformIndex, float levelFraction)
        : waveform {"Waveform",
                    {"Sine", "Saw", "Triangle", "Pulse", "Square", "Noise"},
                    waveformIndex,
                    v1}
        , level {"Level", levelFraction, v1}
    {
        add(waveform, octave, semitone, fine, level);
        add(unisonVoices, unisonDetune, unisonSpread);
    }

    ChoiceParam waveform;
    OctaveParam octave {"Octave", 0, v1};
    SemitoneParam semitone {"Semitone", 0, v1};
    FloatParam fine {"Fine", -100.f, 100.f, 0.f, v1};
    PercentParam level;

    ChoiceParam unisonVoices {
        "Unison Voices", {"1", "2", "3", "4", "5", "6", "7"}, 0, v1};
    FloatParam unisonDetune {"Unison Detune", 0.f, 100.f, 20.f, v1};
    PercentParam unisonSpread {"Unison Spread", 0.6f, v1};
};

struct VALfoParams : ParameterGroup
{
    VALfoParams()
    {
        add(rate, shape, toPitch, toCutoff, toReso, toAmp, toWidth);
        add(toLfoRate);
        add(polarity, retrigger);
    }

    FloatParam rate {"Rate", 0.f, 1.f, 0.6388095021247864f, v1};
    ChoiceParam polarity {"Polarity", {"Bipolar", "Unipolar"}, 0, v1};
    BoolParam retrigger {"Retrigger", false, "Free", "Retrig", v1};
    ChoiceParam shape {"Shape",
                       {"Sine",
                        "Triangle",
                        "Saw",
                        "Rev Saw",
                        "Square",
                        "Steps",
                        "S&H",
                        "Drift",
                        "Noise"},
                       0,
                       v1};

    BipolarParam toPitch {"To Pitch", 0.f, v1};
    BipolarParam toCutoff {"To Cutoff", 0.f, v1};
    BipolarParam toReso {"To Reso", 0.f, v1};
    BipolarParam toAmp {"To Amp", 0.f, v1};
    BipolarParam toWidth {"To Width", 0.f, v1};

    std::array<BipolarParam, 4> toLfoRate {BipolarParam {"To LFO 1 Rate", 0.f, v1},
                                           BipolarParam {"To LFO 2 Rate", 0.f, v1},
                                           BipolarParam {"To LFO 3 Rate", 0.f, v1},
                                           BipolarParam {"To LFO 4 Rate", 0.f, v1}};
};

struct VAParams : ParameterGroup
{
    VAParams()
    {
        addChild("Osc 1", osc1, {.id = "Osc1"});
        addChild("Osc 2", osc2, {.id = "Osc2"});
        addChild("Osc 3", osc3, {.id = "Osc3"});
        addChild("Osc 4", osc4, {.id = "Osc4"});

        add(ampAttack, ampDecay, ampSustain, ampRelease);
        add(filterAttack, filterDecay, filterSustain, filterRelease);
        add(cutoff, resonance, filterEnvAmount, resoEnvAmount, keyTracking);
        add(pulseWidth);

        for (auto i = 0; i < static_cast<int>(lfos.size()); ++i)
        {
            auto n = std::to_string(i + 1);
            addChild("LFO " + n, lfos[i], {.id = "Lfo" + n});
        }

        add(masterLevel);
        add(voiceMode, portamento);
    }

    VAOscParams osc1 {1, 1.f};
    VAOscParams osc2 {1, 0.f};
    VAOscParams osc3 {1, 0.f};
    VAOscParams osc4 {1, 0.f};

    TimeParam ampAttack {"Amp Attack", 0.001f, 5.f, 0.005f, v1};
    TimeParam ampDecay {"Amp Decay", 0.001f, 5.f, 0.12f, v1};
    PercentParam ampSustain {"Amp Sustain", 0.8f, v1};
    TimeParam ampRelease {"Amp Release", 0.001f, 5.f, 0.25f, v1};

    TimeParam filterAttack {"Filter Attack", 0.001f, 5.f, 0.005f, v1};
    TimeParam filterDecay {"Filter Decay", 0.001f, 5.f, 0.2f, v1};
    PercentParam filterSustain {"Filter Sustain", 0.f, v1};
    TimeParam filterRelease {"Filter Release", 0.001f, 5.f, 0.25f, v1};

    FloatParam cutoff {"Cutoff", 0.f, 1.f, 0.7253637313842773f, v1};
    PercentParam resonance {"Resonance", 0.2f, v1};
    BipolarParam filterEnvAmount {"Filter Env Amount", 0.4f, v1};
    BipolarParam resoEnvAmount {"Reso Env Amount", 0.f, v1};
    PercentParam keyTracking {"Key Tracking", 0.f, v1};

    PercentParam pulseWidth {"Pulse Width", 0.5f, v1};

    ChoiceParam voiceMode {"Voice Mode", {"Poly", "Mono", "Legato"}, 0, v1};
    TimeParam portamento {"Portamento", 0.f, 1.f, 0.f, v1};

    std::array<VALfoParams, 4> lfos;

    PercentParam masterLevel {"Master Level", 0.8f, v1};
};

// What Plug's forEachLeaf yields for VirtualAnalogParams: the flat id, the
// display name, the range, the default and the step count, at Plug's ParamID.
struct PlugLeaf
{
    std::string_view flatId;
    std::string_view displayName;
    float minValue;
    float maxValue;
    float defaultValue;
    int numSteps;
};

// clang-format off
constexpr PlugLeaf plugVirtualAnalogLeaves[] = {
    {"Osc1_Waveform", "Osc 1 Waveform", 0, 5, 1, 5},
    {"Osc1_Octave", "Osc 1 Octave", 0, 4, 2, 4},
    {"Osc1_Semitone", "Osc 1 Semitone", -12, 12, 0, 0},
    {"Osc1_Fine", "Osc 1 Fine", -100, 100, 0, 0},
    {"Osc1_Level", "Osc 1 Level", 0, 1, 1, 0},
    {"Osc1_Unison Voices", "Osc 1 Unison Voices", 0, 6, 0, 6},
    {"Osc1_Unison Detune", "Osc 1 Unison Detune", 0, 100, 20, 0},
    {"Osc1_Unison Spread", "Osc 1 Unison Spread", 0, 1, 0.600000024, 0},
    {"Osc2_Waveform", "Osc 2 Waveform", 0, 5, 1, 5},
    {"Osc2_Octave", "Osc 2 Octave", 0, 4, 2, 4},
    {"Osc2_Semitone", "Osc 2 Semitone", -12, 12, 0, 0},
    {"Osc2_Fine", "Osc 2 Fine", -100, 100, 0, 0},
    {"Osc2_Level", "Osc 2 Level", 0, 1, 0, 0},
    {"Osc2_Unison Voices", "Osc 2 Unison Voices", 0, 6, 0, 6},
    {"Osc2_Unison Detune", "Osc 2 Unison Detune", 0, 100, 20, 0},
    {"Osc2_Unison Spread", "Osc 2 Unison Spread", 0, 1, 0.600000024, 0},
    {"Osc3_Waveform", "Osc 3 Waveform", 0, 5, 1, 5},
    {"Osc3_Octave", "Osc 3 Octave", 0, 4, 2, 4},
    {"Osc3_Semitone", "Osc 3 Semitone", -12, 12, 0, 0},
    {"Osc3_Fine", "Osc 3 Fine", -100, 100, 0, 0},
    {"Osc3_Level", "Osc 3 Level", 0, 1, 0, 0},
    {"Osc3_Unison Voices", "Osc 3 Unison Voices", 0, 6, 0, 6},
    {"Osc3_Unison Detune", "Osc 3 Unison Detune", 0, 100, 20, 0},
    {"Osc3_Unison Spread", "Osc 3 Unison Spread", 0, 1, 0.600000024, 0},
    {"Osc4_Waveform", "Osc 4 Waveform", 0, 5, 1, 5},
    {"Osc4_Octave", "Osc 4 Octave", 0, 4, 2, 4},
    {"Osc4_Semitone", "Osc 4 Semitone", -12, 12, 0, 0},
    {"Osc4_Fine", "Osc 4 Fine", -100, 100, 0, 0},
    {"Osc4_Level", "Osc 4 Level", 0, 1, 0, 0},
    {"Osc4_Unison Voices", "Osc 4 Unison Voices", 0, 6, 0, 6},
    {"Osc4_Unison Detune", "Osc 4 Unison Detune", 0, 100, 20, 0},
    {"Osc4_Unison Spread", "Osc 4 Unison Spread", 0, 1, 0.600000024, 0},
    {"Amp Attack", "Amp Attack", 0.001, 5, 0.00499999989, 0},
    {"Amp Decay", "Amp Decay", 0.001, 5, 0.119999997, 0},
    {"Amp Sustain", "Amp Sustain", 0, 1, 0.800000012, 0},
    {"Amp Release", "Amp Release", 0.001, 5, 0.25, 0},
    {"Filter Attack", "Filter Attack", 0.001, 5, 0.00499999989, 0},
    {"Filter Decay", "Filter Decay", 0.001, 5, 0.200000003, 0},
    {"Filter Sustain", "Filter Sustain", 0, 1, 0, 0},
    {"Filter Release", "Filter Release", 0.001, 5, 0.25, 0},
    {"Cutoff", "Cutoff", 0, 1, 0.725363731, 0},
    {"Resonance", "Resonance", 0, 1, 0.200000003, 0},
    {"Filter Env Amount", "Filter Env Amount", -1, 1, 0.400000006, 0},
    {"Reso Env Amount", "Reso Env Amount", -1, 1, 0, 0},
    {"Key Tracking", "Key Tracking", 0, 1, 0, 0},
    {"Pulse Width", "Pulse Width", 0, 1, 0.5, 0},
    {"Lfo1_Rate", "LFO 1 Rate", 0, 1, 0.638809502, 0},
    {"Lfo1_Shape", "LFO 1 Shape", 0, 8, 0, 8},
    {"Lfo1_To Pitch", "LFO 1 To Pitch", -1, 1, 0, 0},
    {"Lfo1_To Cutoff", "LFO 1 To Cutoff", -1, 1, 0, 0},
    {"Lfo1_To Reso", "LFO 1 To Reso", -1, 1, 0, 0},
    {"Lfo1_To Amp", "LFO 1 To Amp", -1, 1, 0, 0},
    {"Lfo1_To Width", "LFO 1 To Width", -1, 1, 0, 0},
    {"Lfo1_To LFO 1 Rate", "LFO 1 To LFO 1 Rate", -1, 1, 0, 0},
    {"Lfo1_To LFO 2 Rate", "LFO 1 To LFO 2 Rate", -1, 1, 0, 0},
    {"Lfo1_To LFO 3 Rate", "LFO 1 To LFO 3 Rate", -1, 1, 0, 0},
    {"Lfo1_To LFO 4 Rate", "LFO 1 To LFO 4 Rate", -1, 1, 0, 0},
    {"Lfo1_Polarity", "LFO 1 Polarity", 0, 1, 0, 1},
    {"Lfo1_Retrigger", "LFO 1 Retrigger", 0, 1, 0, 1},
    {"Lfo2_Rate", "LFO 2 Rate", 0, 1, 0.638809502, 0},
    {"Lfo2_Shape", "LFO 2 Shape", 0, 8, 0, 8},
    {"Lfo2_To Pitch", "LFO 2 To Pitch", -1, 1, 0, 0},
    {"Lfo2_To Cutoff", "LFO 2 To Cutoff", -1, 1, 0, 0},
    {"Lfo2_To Reso", "LFO 2 To Reso", -1, 1, 0, 0},
    {"Lfo2_To Amp", "LFO 2 To Amp", -1, 1, 0, 0},
    {"Lfo2_To Width", "LFO 2 To Width", -1, 1, 0, 0},
    {"Lfo2_To LFO 1 Rate", "LFO 2 To LFO 1 Rate", -1, 1, 0, 0},
    {"Lfo2_To LFO 2 Rate", "LFO 2 To LFO 2 Rate", -1, 1, 0, 0},
    {"Lfo2_To LFO 3 Rate", "LFO 2 To LFO 3 Rate", -1, 1, 0, 0},
    {"Lfo2_To LFO 4 Rate", "LFO 2 To LFO 4 Rate", -1, 1, 0, 0},
    {"Lfo2_Polarity", "LFO 2 Polarity", 0, 1, 0, 1},
    {"Lfo2_Retrigger", "LFO 2 Retrigger", 0, 1, 0, 1},
    {"Lfo3_Rate", "LFO 3 Rate", 0, 1, 0.638809502, 0},
    {"Lfo3_Shape", "LFO 3 Shape", 0, 8, 0, 8},
    {"Lfo3_To Pitch", "LFO 3 To Pitch", -1, 1, 0, 0},
    {"Lfo3_To Cutoff", "LFO 3 To Cutoff", -1, 1, 0, 0},
    {"Lfo3_To Reso", "LFO 3 To Reso", -1, 1, 0, 0},
    {"Lfo3_To Amp", "LFO 3 To Amp", -1, 1, 0, 0},
    {"Lfo3_To Width", "LFO 3 To Width", -1, 1, 0, 0},
    {"Lfo3_To LFO 1 Rate", "LFO 3 To LFO 1 Rate", -1, 1, 0, 0},
    {"Lfo3_To LFO 2 Rate", "LFO 3 To LFO 2 Rate", -1, 1, 0, 0},
    {"Lfo3_To LFO 3 Rate", "LFO 3 To LFO 3 Rate", -1, 1, 0, 0},
    {"Lfo3_To LFO 4 Rate", "LFO 3 To LFO 4 Rate", -1, 1, 0, 0},
    {"Lfo3_Polarity", "LFO 3 Polarity", 0, 1, 0, 1},
    {"Lfo3_Retrigger", "LFO 3 Retrigger", 0, 1, 0, 1},
    {"Lfo4_Rate", "LFO 4 Rate", 0, 1, 0.638809502, 0},
    {"Lfo4_Shape", "LFO 4 Shape", 0, 8, 0, 8},
    {"Lfo4_To Pitch", "LFO 4 To Pitch", -1, 1, 0, 0},
    {"Lfo4_To Cutoff", "LFO 4 To Cutoff", -1, 1, 0, 0},
    {"Lfo4_To Reso", "LFO 4 To Reso", -1, 1, 0, 0},
    {"Lfo4_To Amp", "LFO 4 To Amp", -1, 1, 0, 0},
    {"Lfo4_To Width", "LFO 4 To Width", -1, 1, 0, 0},
    {"Lfo4_To LFO 1 Rate", "LFO 4 To LFO 1 Rate", -1, 1, 0, 0},
    {"Lfo4_To LFO 2 Rate", "LFO 4 To LFO 2 Rate", -1, 1, 0, 0},
    {"Lfo4_To LFO 3 Rate", "LFO 4 To LFO 3 Rate", -1, 1, 0, 0},
    {"Lfo4_To LFO 4 Rate", "LFO 4 To LFO 4 Rate", -1, 1, 0, 0},
    {"Lfo4_Polarity", "LFO 4 Polarity", 0, 1, 0, 1},
    {"Lfo4_Retrigger", "LFO 4 Retrigger", 0, 1, 0, 1},
    {"Master Level", "Master Level", 0, 1, 0.800000012, 0},
    {"Voice Mode", "Voice Mode", 0, 2, 0, 2},
    {"Portamento", "Portamento", 0, 1, 0, 0},
};

// Plug's Session document with Osc 1 at 4 unison voices; Osc 2 a Pulse an
// octave up, -5 semitones, +12.5 cents; Osc 4 at 25 %; Amp Release 1.5 s;
// Cutoff at 0.75; Filter Env Amount -50 %; LFO 3 S&H, retriggered, -25 % onto
// LFO 2's rate; LFO 4 unipolar; Legato; Portamento 125 ms.
constexpr auto plugVirtualAnalogDocument = std::string_view {
    R"({"params":{"Amp Attack":0.004999999888241291,"Amp Decay":0.11999999731779099,"Amp Release":1.5,"Amp Sustain":0.800000011920929,"Cutoff":0.75,"Filter Attack":0.004999999888241291,"Filter Decay":0.20000000298023224,"Filter Env Amount":-0.5,"Filter Release":0.25,"Filter Sustain":0,"Key Tracking":0,"Lfo1":{"Polarity":"Bipolar","Rate":0.6388095021247864,"Retrigger":0,"Shape":"Sine","To Amp":0,"To Cutoff":0,"To LFO 1 Rate":0,"To LFO 2 Rate":0,"To LFO 3 Rate":0,"To LFO 4 Rate":0,"To Pitch":0,"To Reso":0,"To Width":0},"Lfo2":{"Polarity":"Bipolar","Rate":0.6388095021247864,"Retrigger":0,"Shape":"Sine","To Amp":0,"To Cutoff":0,"To LFO 1 Rate":0,"To LFO 2 Rate":0,"To LFO 3 Rate":0,"To LFO 4 Rate":0,"To Pitch":0,"To Reso":0,"To Width":0},"Lfo3":{"Polarity":"Bipolar","Rate":0.6388095021247864,"Retrigger":1,"Shape":"S&H","To Amp":0,"To Cutoff":0,"To LFO 1 Rate":0,"To LFO 2 Rate":-0.25,"To LFO 3 Rate":0,"To LFO 4 Rate":0,"To Pitch":0,"To Reso":0,"To Width":0},"Lfo4":{"Polarity":"Unipolar","Rate":0.6388095021247864,"Retrigger":0,"Shape":"Sine","To Amp":0,"To Cutoff":0,"To LFO 1 Rate":0,"To LFO 2 Rate":0,"To LFO 3 Rate":0,"To LFO 4 Rate":0,"To Pitch":0,"To Reso":0,"To Width":0},"Master Level":0.800000011920929,"Osc1":{"Fine":0,"Level":1,"Octave":"0","Semitone":0,"Unison Detune":20,"Unison Spread":0.6000000238418579,"Unison Voices":"4","Waveform":"Saw"},"Osc2":{"Fine":12.5,"Level":0,"Octave":"+1","Semitone":-5,"Unison Detune":20,"Unison Spread":0.6000000238418579,"Unison Voices":"1","Waveform":"Pulse"},"Osc3":{"Fine":0,"Level":0,"Octave":"0","Semitone":0,"Unison Detune":20,"Unison Spread":0.6000000238418579,"Unison Voices":"1","Waveform":"Saw"},"Osc4":{"Fine":0,"Level":0.25,"Octave":"0","Semitone":0,"Unison Detune":20,"Unison Spread":0.6000000238418579,"Unison Voices":"1","Waveform":"Saw"},"Portamento":0.125,"Pulse Width":0.5,"Reso Env Amount":0,"Resonance":0.20000000298023224,"Voice Mode":"Legato"},"version":1})"};
// clang-format on

void setVirtualAnalogValues(VAParams& params)
{
    params.osc1.unisonVoices.setValue(3.f);
    params.osc2.waveform.setValue(3.f);
    params.osc2.octave.setValue(3.f);
    params.osc2.semitone.setValue(-5.f);
    params.osc2.fine.setValue(12.5f);
    params.osc4.level.setValue(0.25f);
    params.ampRelease.setValue(1.5f);
    params.cutoff.setValue(0.75f);
    params.filterEnvAmount.setValue(-0.5f);
    params.lfos[2].shape.setValue(6.f);
    params.lfos[2].retrigger.setOn(true);
    params.lfos[2].toLfoRate[1].setValue(-0.25f);
    params.lfos[3].polarity.setValue(1.f);
    params.voiceMode.setValue(2.f);
    params.portamento.setValue(0.125f);
}

bool holdsVirtualAnalogValues(const VAParams& params)
{
    using TamberPlugFixtures::plugNear;

    return params.osc1.unisonVoices.getIndex() == 3
           && params.osc2.waveform.getIndex() == 3
           && params.osc2.octave.octaves() == 1
           && params.osc2.semitone.semitones() == -5
           && plugNear(params.osc2.fine.get(), 12.5f)
           && plugNear(params.osc1.level.get(), 1.f)
           && plugNear(params.osc4.level.get(), 0.25f)
           && plugNear(params.ampRelease.get(), 1.5f)
           && plugNear(params.cutoff.get(), 0.75f)
           && plugNear(params.filterEnvAmount.get(), -0.5f)
           && params.lfos[2].shape.getIndex() == 6 && params.lfos[2].retrigger.isOn()
           && plugNear(params.lfos[2].toLfoRate[1].get(), -0.25f)
           && !params.lfos[1].retrigger.isOn()
           && params.lfos[3].polarity.getIndex() == 1
           && params.voiceMode.getIndex() == 2
           && plugNear(params.portamento.get(), 0.125f)
           && plugNear(params.lfos[0].rate.get(), 0.6388095f);
}

// --- A hidden subtree and a session parameter ---------------------------------

// Plug's: addParam(level); addSessionParam(hand); addParam(internal,
// "Internal", false); addParam(tone).
struct PartParams : ParameterGroup
{
    PartParams() { add(level, hand, internal, tone); }

    FloatParam level {"Level", 0.f, 1.f, 0.5f, v1};
    ChoiceParam hand {"Hand",
                      {"Off", "Left", "Right"},
                      1,
                      {.sessionOnly = true, .versionHint = 1}};
    FloatParam internal {
        "Internal", 0.f, 1.f, 0.f, {.automatable = false, .versionHint = 1}};
    BoolParam tone {"Tone", false, v1};
};

// Plug's: addParam(gain); addChild("A", "Part A", partA); addChild("B",
// "Part B", partB, false); addParam(mix).
struct PartsParams : ParameterGroup
{
    PartsParams()
    {
        add(gain);
        addChild("Part A", partA, {.id = "A"});
        addChild("Part B", partB, {.id = "B", .automatable = false});
        add(mix);
    }

    FloatParam gain {"Gain", 0.f, 1.f, 0.5f, v1};
    PartParams partA;
    PartParams partB;
    FloatParam mix {"Mix", 0.f, 1.f, 1.f, v1};
};

struct PlugPartLeaf
{
    std::string_view id;
    std::string_view displayName;
    bool automatable;
};

constexpr PlugPartLeaf plugPartsLeaves[] = {
    {"Gain", "Gain", true},
    {"A/Level", "Part A Level", true},
    {"A/Hand", "Part A Hand", true},
    {"A/Internal", "Part A Internal", false},
    {"A/Tone", "Part A Tone", true},
    {"B/Level", "Part B Level", false},
    {"B/Hand", "Part B Hand", false},
    {"B/Internal", "Part B Internal", false},
    {"B/Tone", "Part B Tone", false},
    {"Mix", "Mix", true},
};

// Part A's Level 0.25 and Hand Right, Part B's Internal 0.75 and Tone on.
constexpr auto plugPartsSession = std::string_view {
    R"({"params":{"A":{"Hand":"Right","Internal":0,"Level":0.25,"Tone":0},"B":{"Hand":"Left","Internal":0.75,"Level":0.5,"Tone":1},"Gain":0.5,"Mix":1},"version":1})"};

constexpr auto plugPartsPreset = std::string_view {
    R"({"params":{"A":{"Internal":0,"Level":0.25,"Tone":0},"B":{"Internal":0.75,"Level":0.5,"Tone":1},"Gain":0.5,"Mix":1},"version":1})"};

void setPartsValues(PartsParams& params)
{
    params.partA.level.setValue(0.25f);
    params.partA.hand.setValue(2.f);
    params.partB.internal.setValue(0.75f);
    params.partB.tone.setOn(true);
}

// --- Helpers --------------------------------------------------------------------

template <typename ParamsT>
struct InstrumentPort : StatePlugin<State<ParamsT>>
{
    std::string_view name() const override { return "InstrumentPort"; }
    BusLayout getBusLayout() const override { return BusLayout::stereoOut(); }
    void prepare(const ProcessSpec&) override {}
    void process(ProcessContext&) noexcept override {}
};

template <typename P>
PluginDescription plugPort()
{
    return {.name = "Plug Port",
            .category = Category::Instrument,
            .pluginCode = "Plgs",
            .create = [] { return OwningPointer<Plugin>(EA::makeOwned<P>()); },
            .idScheme = IdScheme::TamberPlug};
}

OwningPointer<PluginWrapper> wrap(const PluginDescription& description)
{
    return EA::makeOwned<PluginWrapper>(description.create(),
                                        PluginFormat::Unknown,
                                        parameterIdentityFor(description));
}

std::string pathOf(std::string_view flatId)
{
    auto path = std::string {flatId};
    auto join = path.find('_');

    if (join != std::string::npos)
        path[join] = '/';

    return path;
}

// Whether a port's `params` says what Plug's does: the same keys at every
// level, the same choice names, the same numbers, and a JSON bool wherever Plug
// wrote a bool's 0 or 1.
bool samePlugDocument(const Miro::Json::Value& plug, const Miro::Json::Value& port)
{
    if (plug.isObject())
    {
        if (!port.isObject() || plug.asObject().size() != port.asObject().size())
            return false;

        for (const auto& [key, value]: plug.asObject())
        {
            const auto* other = Miro::Json::find(port.asObject(), key);

            if (other == nullptr || !samePlugDocument(value, *other))
                return false;
        }

        return true;
    }

    if (plug.isString())
        return port.isString() && port.asString() == plug.asString();

    if (!plug.isNumber())
        return false;

    if (port.isBool())
        return plug.asNumber() == (port.asBool() ? 1.0 : 0.0);

    return port.isNumber() && std::abs(port.asNumber() - plug.asNumber()) < 1e-6;
}

bool sameParams(std::string_view plugDocument, std::string_view portDocument)
{
    auto plug = Miro::Json::getParsedValue(plugDocument);
    auto port = Miro::Json::getParsedValue(portDocument);

    if (!plug.isObject() || !port.isObject())
        return false;

    const auto* plugParams = Miro::Json::find(plug.asObject(), "params");
    const auto* portParams = Miro::Json::find(port.asObject(), "params");

    return plugParams != nullptr && portParams != nullptr
           && samePlugDocument(*plugParams, *portParams);
}

using VAPort = InstrumentPort<VAParams>;
using PartsPort = InstrumentPort<PartsParams>;

// --- Cases ----------------------------------------------------------------------

auto tVAIds = test("TamberPlugState/aNestedPortKeepsPlugsIdsNamesAndRanges") = []
{
    auto wrapper = wrap(plugPort<VAPort>());
    const auto& list = wrapper->plugin().parameters();
    constexpr auto count = static_cast<int>(std::size(plugVirtualAnalogLeaves));

    check(count == 101);
    check(list.size() >= count);

    for (auto index = 0; index < count; ++index)
    {
        const auto& plug = plugVirtualAnalogLeaves[index];
        const auto& entry = list.entry(index);
        const auto& param = *entry.param;

        check(entry.hostId == static_cast<uint32_t>(index));
        check(entry.id == pathOf(plug.flatId));
        check(entry.displayName == plug.displayName);
        check(list.isRegistered(index));
        check(param.minValue() == plug.minValue);
        check(param.maxValue() == plug.maxValue);
        check(TamberPlugFixtures::plugNear(
            param.defaultValue(), plug.defaultValue, 1e-6f));
        check(param.numSteps() == plug.numSteps);
        check(param.curve() == Parameter::Curve::Linear);
    }

    check(list.identityProblems().empty());
};

auto tVALoadsPlug = test("TamberPlugState/plugsNestedDocumentLoadsIntoThePort") = []
{
    // Plug's VST3 state, as the adapter hands it over.
    auto port = VAPort {};
    port.loadState(plugVirtualAnalogDocument, StateContext::Session);
    check(holdsVirtualAnalogValues(port.params));

    // Plug's AU session, as the wrapper meets it.
    auto foreign = ForeignState {.origin = ForeignOrigin::TamberPlugAU,
                                 .bytes = plugVirtualAnalogDocument,
                                 .context = StateContext::Session};
    auto wrapper = wrap(plugPort<VAPort>());
    check(wrapper->loadForeignState(foreign));
    check(holdsVirtualAnalogValues(static_cast<VAPort&>(wrapper->plugin()).params));
};

auto tVAShape = test("TamberPlugState/thePortWritesPlugsDocumentShape") = []
{
    auto port = VAPort {};
    setVirtualAnalogValues(port.params);
    auto document = port.saveState(StateContext::Session);

    check(sameParams(plugVirtualAnalogDocument, document));

    // A bool is a JSON bool where Plug wrote 1; the port's own next build reads
    // either.
    check(document.find(R"("Lfo3":{)") != std::string::npos);
    check(document.find(R"("Retrigger":true)") != std::string::npos);

    auto next = VAPort {};
    next.loadState(document, StateContext::Session);
    check(holdsVirtualAnalogValues(next.params));
};

auto tPartsIds =
    test("TamberPlugState/hiddenAndSessionLeavesTakeTheirPlugIndex") = []
{
    auto wrapper = wrap(plugPort<PartsPort>());
    const auto& list = wrapper->plugin().parameters();

    for (auto index = 0; index < static_cast<int>(std::size(plugPartsLeaves));
         ++index)
    {
        const auto& plug = plugPartsLeaves[index];
        const auto& entry = list.entry(index);

        check(entry.hostId == static_cast<uint32_t>(index));
        check(entry.id == plug.id);
        check(entry.displayName == plug.displayName);
        check(entry.automatable == plug.automatable);
        check(list.isRegistered(index) == plug.automatable);
    }

    check(list.entry(2).sessionOnly && list.entry(6).sessionOnly);
    check(list.identityProblems().empty());
};

auto tPartsState =
    test("TamberPlugState/sessionLeavesMoveOnlyInASessionAsInPlug") = []
{
    auto saved = PartsPort {};
    setPartsValues(saved.params);

    check(sameParams(plugPartsSession, saved.saveState(StateContext::Session)));
    check(sameParams(plugPartsPreset, saved.saveState(StateContext::Preset)));

    auto session = PartsPort {};
    session.loadState(plugPartsSession, StateContext::Session);
    const auto& params = session.params;
    check(TamberPlugFixtures::plugNear(params.partA.level.get(), 0.25f));
    check(params.partA.hand.getIndex() == 2);
    check(params.partB.hand.getIndex() == 1);
    check(TamberPlugFixtures::plugNear(params.partB.internal.get(), 0.75f));
    check(params.partB.tone.isOn() && !params.partA.tone.isOn());

    // A preset leaves a session parameter where it was.
    auto preset = PartsPort {};
    preset.params.partA.hand.setValue(0.f);
    preset.loadState(plugPartsPreset, StateContext::Preset);
    check(preset.params.partA.hand.getIndex() == 0);
    check(TamberPlugFixtures::plugNear(preset.params.partA.level.get(), 0.25f));
};
} // namespace
