// Hosting VST3 plugins: the fixture plugins (Tests/HostFixtures) loaded by class
// id through the FormatManager, their buses and parameters (ids, text through the
// controller, renames, latency), the begin/perform/end of the plugin's own GUI
// reaching the listener, output parameters, MIDI both ways, the state round trip,
// the fallback playhead and buses the caller does not supply.

#include <MakeASound/Plugin/VST3/VST3Common.h>

#include <MakeASound/Hosting/MakeASoundHosting.h>
#include <MakeASound/Hosting/VST3/VST3HostedPlugin.h>

#include <NanoTest/NanoTest.h>

#include <cmath>
#include <filesystem>
#include <string>

#ifndef MAKEASOUND_HOST_FIXTURE_VST3_DIR
#error "MAKEASOUND_HOST_FIXTURE_VST3_DIR must name the fixtures' VST3 folder"
#endif

namespace HostedVST3Tests
{
using namespace nano;
using namespace MakeASound;
using namespace MakeASound::Hosting;

namespace
{
namespace fs = std::filesystem;

constexpr auto blockSize = 64;

struct DefaultFormats : FormatManager
{
    DefaultFormats() { addDefaultFormats(); }
};

std::string fixture(const char* name)
{
    const auto folder = fs::path(MAKEASOUND_HOST_FIXTURE_VST3_DIR);

    return (folder / (std::string(name) + ".vst3")).string();
}

Hosting::PluginDescription describe(const FormatManager& formats, const char* name)
{
    auto scan = formats.scanTarget(fixture(name)).scan;
    check(scan.classes.size() == 1);
    return scan.classes.empty() ? Hosting::PluginDescription() : scan.classes[0];
}

OwningPointer<HostedPlugin> load(const FormatManager& formats, const char* name)
{
    auto result = formats.load(describe(formats, name), {.name = "Hosting Tests"});
    check(result.error.empty());
    check(result.plugin != nullptr);
    return std::move(result.plugin);
}

int indexNamed(const Plugin& plugin, std::string_view name)
{
    const auto& list = plugin.parameters();

    for (auto i = 0; i < list.size(); ++i)
        if (list[i].name() == name)
            return i;

    return -1;
}

void copyState(HostedPlugin& from, HostedPlugin& to)
{
    to.loadState(from.saveState(StateContext::Session), StateContext::Session);
}

bool near(float a, float b, float tolerance = 1e-4f)
{
    return std::abs(a - b) < tolerance;
}

// A context over the plugin's own buses, every one supplied unless told.
struct Block
{
    explicit Block(Plugin& pluginToUse,
                   bool supplyAux = true,
                   double sampleRate = 48000.0)
        : plugin(pluginToUse)
    {
        auto layout = plugin.getBusLayout();
        context.prepare(layout);

        for (auto i = 0; i < layout.inputs.size(); ++i)
            if (supplyAux || layout.inputs[i].isMain)
                context.inputs[i].setSize(layout.inputs[i].numChannels, blockSize);

        for (auto i = 0; i < layout.outputs.size(); ++i)
            context.outputs[i].setSize(layout.outputs[i].numChannels, blockSize);

        plugin.prepare(
            {.sampleRate = sampleRate, .maxBlockSize = blockSize, .layout = layout});
    }

    void fillInput(int bus, float value)
    {
        for (auto channel: context.inputs[bus])
            for (auto& sample: channel)
                sample = value;
    }

    void run()
    {
        context.clearMidiOut();
        plugin.process(context);
    }

    Plugin& plugin;
    ProcessContext context;
};

struct EditRecorder : HostEditListener
{
    void beginParameterEdit(int index) noexcept override
    {
        events.add("begin " + std::to_string(index));
    }

    void performParameterEdit(int index, float value) noexcept override
    {
        events.add("perform " + std::to_string(index));
        lastValue = value;
    }

    void endParameterEdit(int index) noexcept override
    {
        events.add("end " + std::to_string(index));
    }

    void parameterInfoChanged() noexcept override { ++infos; }
    void latencyChanged() noexcept override { ++latencies; }

    Vector<std::string> events;
    float lastValue = -1.f;
    int infos = 0;
    int latencies = 0;
};

auto tLoad = test("HostedVST3/aPluginLoadsByItsClassId") = []
{
    const auto formats = DefaultFormats();
    const auto description = describe(formats, "HostFixtureEffect");
    auto plugin = load(formats, "HostFixtureEffect");

    check(plugin->name() == "HostFixtureEffect");
    check(plugin->description().classId == description.classId);
    check(plugin->description().path == fixture("HostFixtureEffect"));
    check(!plugin->wantsHostBypass());

    auto* vst3 = dynamic_cast<VST3HostedPlugin*>(plugin.get());
    check(vst3 != nullptr);
    check(!vst3->hasSeparateController());
    check(vst3->editController() != nullptr);
    check(vst3->componentHandlerInterface() != nullptr);
};

auto tLoadErrors = test("HostedVST3/aLoadThatCannotHappenSaysWhy") = []
{
    const auto formats = DefaultFormats();

    auto wrongClass = describe(formats, "HostFixtureEffect");
    wrongClass.classId = "00000000000000000000000000000000";
    auto noClass = formats.load(wrongClass);
    check(noClass.plugin == nullptr);
    check(noClass.error.find("no audio class") != std::string::npos);

    auto missing = wrongClass;
    missing.path = fixture("Missing");
    check(formats.load(missing).plugin == nullptr);
    check(!formats.load(missing).error.empty());

    auto unhosted = wrongClass;
    unhosted.format = PluginFormat::AAX;
    check(formats.load(unhosted).plugin == nullptr);
};

auto tBuses = test("HostedVST3/theBusesAreThePluginsOwn") = []
{
    const auto formats = DefaultFormats();
    auto plugin = load(formats, "HostFixtureEffect");
    const auto layout = plugin->getBusLayout();

    check(layout.inputs.size() == 2);
    check(layout.inputs[0].numChannels == 2 && layout.inputs[0].isMain);
    check(layout.inputs[0].activeByDefault);
    check(layout.inputs[1].name == "Sidechain");
    check(layout.inputs[1].numChannels == 1 && !layout.inputs[1].isMain);
    check(!layout.inputs[1].activeByDefault);
    check(layout.outputs.size() == 1);
    check(layout.midiInputs.empty());

    check(plugin->acceptsLayout(layout));

    auto fewer = layout;
    fewer.inputs.pop_back();
    check(!plugin->acceptsLayout(fewer));
};

auto tParameters = test("HostedVST3/parametersCarryThePluginsIds") = []
{
    const auto formats = DefaultFormats();
    auto plugin = load(formats, "HostFixtureEffect");
    auto* vst3 = dynamic_cast<VST3HostedPlugin*>(plugin.get());
    const auto& list = plugin->parameters();

    // The fixture's eight, plus the bypass its own wrapper added.
    check(list.size() == 9);
    check(list.identityProblems().empty());
    check(list.bypassIndex() >= 0);
    check(list[list.bypassIndex()].isBypass());

    for (auto i = 0; i < list.size(); ++i)
    {
        const auto& entry = list.entry(i);
        check(entry.id == std::to_string(entry.hostId));
        check(vst3->indexOfParamId(entry.hostId) == i);
    }

    auto cutoff = indexNamed(*plugin, "Cutoff");
    check(cutoff >= 0);
    check(list[cutoff].label() == "Hz");
    check(list[cutoff].minValue() == 0.f && list[cutoff].maxValue() == 1.f);

    // Text through the plugin's controller: its skew and unit.
    auto text = list[cutoff].valueToText(list[cutoff].getValue());
    check(text.find("1000") != std::string::npos);
    check(text.find("Hz") != std::string::npos);

    auto alt = indexNamed(*plugin, "Alt Names");
    check(list[alt].numSteps() == 1);
};

auto tAudio = test("HostedVST3/aBlockRunsAndOutputParametersComeBack") = []
{
    const auto formats = DefaultFormats();
    auto plugin = load(formats, "HostFixtureEffect");
    auto block = Block(*plugin);

    block.fillInput(0, 0.5f);
    block.run();

    check(near(block.context.outputs[0][0][0], 0.5f));
    check(near(block.context.outputs[0][1][blockSize - 1], 0.5f));

    auto peak = indexNamed(*plugin, "Peak");
    check(near(plugin->parameters()[peak].getValue(), 0.5f));

    // Pushed to the plugin's own editor on the message thread.
    plugin->syncEditorWithProcessor();
    auto* vst3 = dynamic_cast<VST3HostedPlugin*>(plugin.get());
    auto id = plugin->parameters().entry(peak).hostId;
    auto inEditor = vst3->editController()->getParamNormalized(id);
    check(near(static_cast<float>(inEditor), 0.5f));
};

auto tUnsupplied = test("HostedVST3/aBusTheCallerDoesNotSupplyIsSilence") = []
{
    const auto formats = DefaultFormats();
    auto plugin = load(formats, "HostFixtureEffect");
    auto block = Block(*plugin, false);

    check(block.context.inputs[1].isEmpty());

    block.fillInput(0, 0.25f);
    block.run();

    check(near(block.context.outputs[0][0][0], 0.25f));
    check(plugin->parameters()[indexNamed(*plugin, "Sidechain")].getValue() == 0.f);

    // A longer block than prepared is refused, not overrun.
    block.context.outputs[0].setSize(2, blockSize * 2);
    block.run();
};

auto tHostEdit = test("HostedVST3/aHostEditReachesTheProcessor") = []
{
    const auto formats = DefaultFormats();
    auto plugin = load(formats, "HostFixtureEffect");
    auto listener = EditRecorder();
    plugin->setHostEditListener(&listener);

    auto block = Block(*plugin);
    auto latency = indexNamed(*plugin, "Latency");

    check(plugin->latencySamples() == 0);
    check(!plugin->takeLatencyChanged());

    // Through the controller: the plugin re-reports its latency.
    plugin->parameters()[latency].setValue(0.5f);

    check(plugin->takeLatencyChanged());
    check(!plugin->takeLatencyChanged());
    check(plugin->latencySamples() == 512);
    check(listener.latencies == 1);

    plugin->setHostEditListener(nullptr);
};

auto tGuiEdits = test("HostedVST3/thePluginsOwnEditsReachTheListener") = []
{
    const auto formats = DefaultFormats();
    auto plugin = load(formats, "HostFixtureEffect");
    auto* vst3 = dynamic_cast<VST3HostedPlugin*>(plugin.get());
    auto listener = EditRecorder();
    plugin->setHostEditListener(&listener);

    auto cutoff = indexNamed(*plugin, "Cutoff");
    auto id = plugin->parameters().entry(cutoff).hostId;
    auto* handler = vst3->componentHandlerInterface();

    // What the plugin's GUI does when the user drags its knob.
    handler->beginEdit(id);
    handler->performEdit(id, 0.75);
    handler->endEdit(id);

    check(listener.events.size() == 3);
    check(listener.events[0] == "begin " + std::to_string(cutoff));
    check(listener.events[1] == "perform " + std::to_string(cutoff));
    check(listener.events[2] == "end " + std::to_string(cutoff));
    check(near(listener.lastValue, 0.75f));
    check(near(plugin->parameters()[cutoff].getValue(), 0.75f));

    // An id the plugin never listed belongs to no index.
    handler->beginEdit(0x7ffffff0u);
    check(listener.events.size() == 3);

    // The edit reaches the processor: its saved state holds it.
    auto block = Block(*plugin);
    block.run();

    auto fresh = load(formats, "HostFixtureEffect");
    copyState(*plugin, *fresh);
    check(near(fresh->parameters()[cutoff].getValue(), 0.75f));

    plugin->setHostEditListener(nullptr);
};

auto tAudioThread = test("HostedVST3/aValueFromTheAudioThreadLandsInTheBlock") = []
{
    const auto formats = DefaultFormats();
    auto plugin = load(formats, "HostFixtureEffect");
    auto cutoff = indexNamed(*plugin, "Cutoff");

    check(!plugin->setParameterFromAudioThread(cutoff, 0.3f));

    auto block = Block(*plugin);

    check(!plugin->setParameterFromAudioThread(-1, 0.3f));
    check(!plugin->setParameterFromAudioThread(plugin->parameters().size(), 0.3f));
    check(plugin->setParameterFromAudioThread(cutoff, 0.3f));
    check(near(plugin->parameters()[cutoff].getValue(), 0.3f));

    block.run();
    plugin->syncEditorWithProcessor();

    auto fresh = load(formats, "HostFixtureEffect");
    copyState(*plugin, *fresh);
    check(near(fresh->parameters()[cutoff].getValue(), 0.3f));
};

auto tRename = test("HostedVST3/aRenameIsReadBackInPlace") = []
{
    const auto formats = DefaultFormats();
    auto plugin = load(formats, "HostFixtureEffect");
    auto listener = EditRecorder();
    plugin->setHostEditListener(&listener);

    auto cutoff = indexNamed(*plugin, "Cutoff");
    auto hostId = plugin->parameters().entry(cutoff).hostId;
    auto alt = indexNamed(*plugin, "Alt Names");

    plugin->parameters()[alt].setValue(1.f);
    check(!plugin->takeParameterInfoChanged());

    // The fixture renames when a state with Alt Names on is loaded.
    copyState(*plugin, *plugin);

    check(plugin->takeParameterInfoChanged());
    check(!plugin->takeParameterInfoChanged());
    check(listener.infos >= 1);
    check(plugin->parameters()[cutoff].name() == "Frequency");
    check(plugin->parameters().entry(cutoff).displayName == "Frequency");
    check(plugin->parameters().entry(cutoff).hostId == hostId);
    check(indexNamed(*plugin, "Frequency") == cutoff);

    plugin->setHostEditListener(nullptr);
};

auto tState = test("HostedVST3/theStateRoundTripsThroughAFreshInstance") = []
{
    const auto formats = DefaultFormats();
    auto source = load(formats, "HostFixtureEffect");
    auto cutoff = indexNamed(*source, "Cutoff");
    auto latency = indexNamed(*source, "Latency");

    source->parameters()[cutoff].setValue(0.2f);
    source->parameters()[latency].setValue(0.25f);

    auto state = source->saveState(StateContext::Preset);
    check(!state.empty());

    auto copy = load(formats, "HostFixtureEffect");
    copy->loadState(state, StateContext::Preset);

    check(near(copy->parameters()[cutoff].getValue(), 0.2f));
    check(near(copy->parameters()[latency].getValue(), 0.25f));

    copy->loadState({}, StateContext::Preset);
    check(near(copy->parameters()[cutoff].getValue(), 0.2f));
};

auto tRate = test("HostedVST3/aFractionalRateReachesThePluginUnrounded") = []
{
    const auto formats = DefaultFormats();
    auto plugin = load(formats, "HostFixtureEffect");
    auto fraction = indexNamed(*plugin, "Rate Fraction");
    const auto& list = plugin->parameters();

    // The fixture is itself a MakeASound VST3: the rate crosses setupProcessing()
    // and its adapter whole.
    auto block = Block(*plugin, true, 44055.94);
    block.run();
    check(near(list[fraction].getValue(), 0.94f));

    auto whole = Block(*plugin, true, 48000.0);
    whole.run();
    check(list[fraction].getValue() == 0.f);
};

auto tPlayhead = test("HostedVST3/theFallbackPlayheadIsStoppedUnlessSet") = []
{
    const auto formats = DefaultFormats();
    auto plugin = load(formats, "HostFixtureEffect");
    auto tempo = indexNamed(*plugin, "Seen Tempo");
    auto playing = indexNamed(*plugin, "Seen Playing");
    const auto& list = plugin->parameters();

    check(!plugin->fallbackPlayhead().isPlaying);
    check(plugin->fallbackPlayhead().bpm == 120.0);

    auto block = Block(*plugin);
    block.run();
    check(near(list[tempo].getValue(), 0.12f));
    check(list[playing].getValue() == 0.f);

    auto fallback = Playhead();
    fallback.isPlaying = true;
    fallback.bpm = 140.0;
    plugin->setFallbackPlayhead(fallback);

    block.run();
    check(near(list[tempo].getValue(), 0.14f));
    check(list[playing].getValue() == 1.f);

    // A host's own transport wins.
    block.context.playhead.isValid = true;
    block.context.playhead.bpm = 90.0;
    block.context.playhead.isPlaying = false;
    block.run();
    check(near(list[tempo].getValue(), 0.09f));
    check(list[playing].getValue() == 0.f);
};

auto tMidi = test("HostedVST3/midiGoesInAndComesBackOut") = []
{
    const auto formats = DefaultFormats();
    auto plugin = load(formats, "HostFixtureInstrument");
    const auto layout = plugin->getBusLayout();

    check(layout.inputs.empty());
    check(layout.midiInputs.size() == 1);
    check(layout.midiOutputs.size() == 1);

    auto block = Block(*plugin);
    block.run();
    check(block.context.outputs[0][0][0] == 0.f);

    block.context.midiIn[0].add(MIDI::Event::noteOn(3, 60, 0.5f, 7));
    block.run();
    block.context.midiIn[0].clear();

    check(near(block.context.outputs[0][0][0], 0.8f));
    check(block.context.midiOut[0].size() == 1);

    const auto& echoed = block.context.midiOut[0][0];
    check(echoed.sampleOffset == 7);
    check(echoed.channel == 3);
    check(echoed.asNoteOn() != nullptr && echoed.asNoteOn()->pitch == 60);
    check(near(echoed.asNoteOn()->velocity, 0.5f, 0.01f));

    block.context.midiIn[0].add(MIDI::Event::noteOff(3, 60, 0.f, 0));
    block.run();
    check(block.context.outputs[0][0][blockSize - 1] == 0.f);
    check(block.context.midiOut[0].size() == 1);
    check(block.context.midiOut[0][0].isNoteOff());
};
} // namespace
} // namespace HostedVST3Tests
