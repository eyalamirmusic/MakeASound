// The CLAP adapter hosted in-process through the C API: the factory and the
// linked clap_entry, the lifecycle, parameter info, ids and text, timed values,
// flush outside process, the editor's gestures and the plugin's own moves,
// rescans, state and its contexts, audio and note ports, latency, tail, offline
// rendering, the bypass, MIDI and SysEx each way, the transport and track info,
// a JUCE port's ids, the message loop pumped from the host's, gestures a full
// host list refused, nested and left at stop_processing, a range change, and
// MIDI out in time order.

#include "CLAPTestHost.h"
#include "JucePort.h"

#include <MakeASound/Plugin/Compat/JuceState.h>
#include <MakeASound/Plugin/Core/Identity.h>
#include <MakeASound/Plugin/Realtime/MessageThread.h>

#include <NanoTest/NanoTest.h>

#include <array>
#include <chrono>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace CLAPTests
{
using namespace nano;
using namespace TestPlugins;
using namespace CLAPHost;

namespace
{
bool near(double a, double b, double tolerance = 1e-4)
{
    return std::abs(a - b) < tolerance;
}

const ModuleDescription& testModule()
{
    static const auto module = []
    {
        auto description = ModuleDescription {};
        description.vendor = "MakeASound Tests";
        description.url = "https://example.com";
        description.manufacturerCode = "MaSo";
        description.plugins.add(
            {.name = "Gain",
             .version = "1.2.3",
             .pluginCode = "Gain",
             .create = [] { return EA::makeOwned<GainPlugin>(); }});
        description.plugins.add(
            {.name = "Synth",
             .category = Category::Instrument,
             .pluginCode = "Synt",
             .create = [] { return EA::makeOwned<SynthPlugin>(); },
             .clapId = "com.example.synth"});
        return description;
    }();

    return module;
}

// Any infinite or finite tail, as set.
struct TailPlugin : GainPlugin
{
    int tailSamples() const noexcept override { return tail; }

    int tail = 0;
};

auto tFactory = test("CLAP/theFactoryDescribesEveryPlugin") = []
{
    auto factory = MakeASound::CLAP::Factory(testModule(), "com.makeasound.test");
    const auto* table = factory.get();

    check(table->get_plugin_count(table) == 2);

    const auto* gain = table->get_plugin_descriptor(table, 0);
    check(gain != nullptr);
    check(std::string(gain->id) == "com.makeasound.test.Gain");
    check(std::string(gain->name) == "Gain");
    check(std::string(gain->vendor) == "MakeASound Tests");
    check(std::string(gain->url) == "https://example.com");
    check(std::string(gain->version) == "1.2.3");
    check(clap_version_is_compatible(gain->clap_version));
    check(std::string(gain->features[0]) == CLAP_PLUGIN_FEATURE_AUDIO_EFFECT);
    check(gain->features[1] == nullptr);

    // The description's own id wins.
    const auto* synth = table->get_plugin_descriptor(table, 1);
    check(std::string(synth->id) == "com.example.synth");
    check(std::string(synth->features[0]) == CLAP_PLUGIN_FEATURE_INSTRUMENT);

    check(table->get_plugin_descriptor(table, 2) == nullptr);
    check(clapIdFor("com.x", testModule().plugins[0]) == "com.x.Gain");
};

auto tFactoryCreates = test("CLAP/createPluginMakesAnAdapterUnderTheCLAPFormat") = []
{
    auto factory = MakeASound::CLAP::Factory(testModule(), "com.makeasound.test");
    const auto* table = factory.get();

    auto host = PluginHost<GainPlugin>(
        [&](const clap_host* h)
        { return table->create_plugin(table, h, "com.makeasound.test.Gain"); },
        0);

    check(host.clap != nullptr && host.initialised);
    check(host.clap->desc == table->get_plugin_descriptor(table, 0));
    check(currentPluginFormat() == PluginFormat::CLAP);
    check(host.plugin().format() == PluginFormat::CLAP);
    check(host.plugin().formatAtPrepare == PluginFormat::CLAP);

    auto testHost = TestHost {};
    check(table->create_plugin(table, &testHost.host, "com.example.unknown")
          == nullptr);

    testHost.host.clap_version = {0, 9, 0};
    check(table->create_plugin(table, &testHost.host, "com.makeasound.test.Gain")
          == nullptr);
};

// The entry compiled into this binary, over PluginTests.cpp's describeModule().
auto tEntry = test("CLAP/theLinkedEntryServesTheModulesFactory") = []
{
    check(clap_version_is_compatible(clap_entry.clap_version));
    check(clap_entry.init("/path/to/Tests.clap"));

    check(clap_entry.get_factory("clap.not-a-factory") == nullptr);

    const auto* table = static_cast<const clap_plugin_factory*>(
        clap_entry.get_factory(CLAP_PLUGIN_FACTORY_ID));
    check(table != nullptr);

    if (table != nullptr)
    {
        check(table->get_plugin_count(table) == 2);
        auto id = std::string(table->get_plugin_descriptor(table, 0)->id);
        check(id == std::string(MakeASound::CLAP::moduleBundleId()) + ".Gain");

        auto host = PluginHost<GainPlugin>(
            [&](const clap_host* h)
            { return table->create_plugin(table, h, id.c_str()); },
            0);
        check(host.clap != nullptr && host.active);
        check(&host.plugin() == Module::lastCreated);

        host.fillBlock(blockSize);
        check(host.run() == CLAP_PROCESS_CONTINUE);
        check(host.plugin().processed == 1);
    }

    clap_entry.deinit();
};

auto tLifecycle = test("CLAP/activationPreparesThePluginAtTheHostsRate") = []
{
    auto host = PluginHost<GainPlugin> {};
    auto& plugin = host.plugin();

    check(host.initialised && host.active && host.processing);
    check(plugin.hostName() == "MakeASound Test Host");
    check(near(plugin.spec.sampleRate, 48000.0));
    check(plugin.spec.maxBlockSize == maxBlock);

    // Activating an active plugin is refused.
    check(!host.clap->activate(host.clap, 44100.0, 1, 32));

    auto resets = plugin.resets;
    check(host.configure(44100.0, 32));
    check(near(plugin.spec.sampleRate, 44100.0));
    check(plugin.spec.maxBlockSize == 32);
    check(plugin.resets > resets);

    resets = plugin.resets;
    host.clap->reset(host.clap);
    check(plugin.resets == resets + 1);
};

auto tBeforeActivation = test("CLAP/aBlockBeforeActivationIsSilent") = []
{
    auto testHost = TestHost {};
    auto* adapter = new MakeASound::CLAP::Adapter(
        &testHost.host, &testDescriptor(), EA::makeOwned<GainPlugin>());
    const auto* clap = adapter->clapPlugin();
    check(clap->init(clap));

    auto channel = std::vector<float>(blockSize, 1.f);
    auto* pointer = channel.data();
    auto output = clap_audio_buffer {};
    output.data32 = &pointer;
    output.channel_count = 1;

    auto data = clap_process {};
    data.frames_count = blockSize;
    data.audio_outputs = &output;
    data.audio_outputs_count = 1;

    check(clap->process(clap, &data) == CLAP_PROCESS_CONTINUE);
    check(channel[0] == 0.f && channel[blockSize - 1] == 0.f);
    check(static_cast<GainPlugin&>(adapter->plugin()).processed == 0);

    clap->destroy(clap);
};

auto tUnity = test("CLAP/aBlockPassesTheInputThroughThePlugin") = []
{
    auto host = PluginHost<GainPlugin> {};
    host.plugin().params.gain.setValue(0.f);

    for (auto& channel: host.inputs[0].storage)
        std::fill(channel.begin(), channel.end(), 0.5f);

    host.fillBlock(blockSize);
    check(host.run() == CLAP_PROCESS_CONTINUE);

    check(near(host.outputs[0].storage[0][0], 0.5f));
    check(near(host.outputs[0].storage[1][blockSize - 1], 0.5f));
    check(host.outputBuffers[0].constant_mask == 0);

    // In place too.
    host.fillBlock(blockSize, true);
    host.run();
    check(near(host.inputs[0].storage[0][3], 0.5f));
};

auto tParameterInfo = test("CLAP/parameterInfoMirrorsTheList") = []
{
    auto host = PluginHost<GainPlugin> {};
    auto& params = host.plugin().params;
    const auto& list = host.plugin().parameters();

    check(host.params->count(host.clap) == 5);

    for (auto i = 0; i < 5; ++i)
    {
        auto info = host.info(i);
        check(info.id == list.entry(i).hostId);
        check((info.flags & CLAP_PARAM_IS_AUTOMATABLE) != 0);
    }

    auto gain = host.infoOf(host.idOf(params.gain));
    check(std::string(gain.name) == "Gain");
    check(std::string(gain.module).empty());
    check(gain.min_value == 0.0 && gain.max_value == 1.0);
    check(near(gain.default_value, params.gain.toNormalized(0.f)));
    check((gain.flags & CLAP_PARAM_IS_STEPPED) == 0);

    // The group is the module.
    auto cutoff = host.infoOf(host.idOf(params.filter.cutoff));
    check(std::string(cutoff.name) == "Cutoff");
    check(std::string(cutoff.module) == "Filter");

    // A choice publishes its plain indices, stepped, as an enum.
    auto mode = host.infoOf(host.idOf(params.mode));
    check(mode.min_value == 0.0 && mode.max_value == 2.0);
    check(mode.default_value == 0.0);
    check((mode.flags & CLAP_PARAM_IS_STEPPED) != 0);
    check((mode.flags & CLAP_PARAM_IS_ENUM) != 0);

    auto bypass = host.infoOf(host.idOf(params.bypass));
    check(bypass.min_value == 0.0 && bypass.max_value == 1.0);
    check((bypass.flags & CLAP_PARAM_IS_STEPPED) != 0);
    check((bypass.flags & CLAP_PARAM_IS_ENUM) == 0);
    check((bypass.flags & CLAP_PARAM_IS_BYPASS) != 0);

    auto none = clap_param_info {};
    check(!host.params->get_info(host.clap, 5, &none));
};

auto tValuesAndText = test("CLAP/valuesAndTextGoThroughTheParameter") = []
{
    auto host = PluginHost<GainPlugin> {};
    auto& params = host.plugin().params;
    auto gainId = host.idOf(params.gain);
    auto modeId = host.idOf(params.mode);

    params.gain.setNormalized(0.25f);
    params.mode.setValue(2.f);
    check(near(host.value(gainId), 0.25));
    check(host.value(modeId) == 2.0);

    auto text = std::array<char, CLAP_NAME_SIZE> {};
    check(host.params->value_to_text(host.clap, modeId, 1.0, text.data(), 64));
    check(std::string(text.data()) == "Warm");

    auto value = 0.0;
    check(host.params->text_to_value(host.clap, modeId, "Hot", &value));
    check(value == 2.0);

    check(host.params->value_to_text(
        host.clap, gainId, params.gain.toNormalized(-6.f), text.data(), 64));
    check(host.params->text_to_value(host.clap, gainId, text.data(), &value));
    check(near(params.gain.toPlain(static_cast<float>(value)), -6.f, 0.1));

    // A short buffer is cut and terminated; an unknown id is refused.
    auto tiny = std::array<char, 3> {'x', 'x', 'x'};
    check(host.params->value_to_text(host.clap, modeId, 1.0, tiny.data(), 3));
    check(std::string(tiny.data()) == "Wa");
    check(!host.params->get_value(host.clap, 12345u, &value));
    check(!host.params->value_to_text(host.clap, 12345u, 0.0, text.data(), 64));
};

auto tTimedValues = test("CLAP/timedValuesLandAtTheirOffsets") = []
{
    auto host = PluginHost<AutomationPlugin> {};
    auto& params = host.plugin().params;
    auto amountId = host.idOf(params.amount);
    auto modeId = host.idOf(params.mode);

    host.in.param(amountId, 0, 0.2);
    host.in.param(amountId, 16, 0.6);
    host.in.param(modeId, 40, 3.0);
    host.fillBlock(blockSize);
    host.run();

    const auto& plugin = host.plugin();
    check(plugin.numChanges == 3);
    check(plugin.changes[0].sampleOffset == 0 && near(plugin.changes[0].value, 0.2));
    check(plugin.changes[1].sampleOffset == 16
          && near(plugin.changes[1].value, 0.6));
    check(plugin.changes[1].rampSamples == 0);
    check(near(plugin.changes[1].previousValue, 0.2));
    check(plugin.changes[2].sampleOffset == 40);
    check(near(plugin.changes[2].value, params.mode.toNormalized(3.f)));

    // The block's end values, and nothing echoed back.
    check(near(params.amount.getNormalized(), 0.6));
    check(params.mode.getValue() == 3.f);
    check(host.out.ofType(CLAP_EVENT_PARAM_VALUE).empty());

    // A time past the block lands on its last sample.
    host.clearEvents();
    host.in.param(amountId, 500, 0.9);
    host.fillBlock(32);
    host.run();
    check(plugin.numChanges == 1 && plugin.changes[0].sampleOffset == 31);
};

auto tFlushInactive = test("CLAP/flushOutsideProcessLandsTheValues") = []
{
    auto host = PluginHost<GainPlugin> {};
    auto& params = host.plugin().params;
    auto gainId = host.idOf(params.gain);
    auto modeId = host.idOf(params.mode);

    // Deactivated: on the main thread.
    host.stop();
    host.in.param(gainId, 0, 0.3);
    host.flush();
    check(near(params.gain.getNormalized(), 0.3));
    check(near(host.value(gainId), 0.3));
    check(host.out.events.empty());

    // Active but not processing: on the audio thread.
    host.configure(48000.0, maxBlock);
    host.clap->stop_processing(host.clap);
    host.processing = false;
    host.clearEvents();
    host.in.param(modeId, 7, 1.0);
    host.flush();
    check(params.mode.getValue() == 1.f);

    // A later block processes normally.
    host.processing = host.clap->start_processing(host.clap);
    host.clearEvents();
    host.in.param(gainId, 0, 0.7);
    host.fillBlock(blockSize);
    host.run();
    check(near(params.gain.getNormalized(), 0.7));
};

auto tGesturesIdle = test("CLAP/anEditorGestureIsFlushedWhenNotProcessing") = []
{
    auto host = PluginHost<GainPlugin> {};
    auto& params = host.plugin().params;
    const auto& list = host.plugin().parameters();
    auto& listener = *host.plugin().hostEditListener();
    auto index = list.indexOf(&params.gain);
    auto gainId = host.idOf(params.gain);

    host.stop();
    host.host.clearRecords();

    listener.beginParameterEdit(index);
    params.gain.setNormalized(0.25f);
    listener.performParameterEdit(index, 0.25f);
    listener.endParameterEdit(index);

    check(host.host.flushRequests >= 1);
    check(host.out.events.empty());

    host.flush();

    const auto& events = host.out.events;
    check(events.size() == 3);

    if (events.size() == 3)
    {
        check(events[0].header.type == CLAP_EVENT_PARAM_GESTURE_BEGIN);
        check(events[0].gesture.param_id == gainId);
        check(events[1].header.type == CLAP_EVENT_PARAM_VALUE);
        check(events[1].param.param_id == gainId);
        check(near(events[1].param.value, 0.25));
        check(events[2].header.type == CLAP_EVENT_PARAM_GESTURE_END);
        check(events[2].gesture.param_id == gainId);
    }

    // Sent once.
    host.clearEvents();
    host.flush();
    check(host.out.events.empty());
};

auto tGesturesProcessing = test("CLAP/anEditorGestureLeavesWithTheNextBlock") = []
{
    auto host = PluginHost<GainPlugin> {};
    auto& params = host.plugin().params;
    const auto& list = host.plugin().parameters();
    auto& listener = *host.plugin().hostEditListener();
    auto index = list.indexOf(&params.mode);
    auto modeId = host.idOf(params.mode);

    host.host.clearRecords();
    listener.beginParameterEdit(index);
    params.mode.setValue(2.f);
    listener.performParameterEdit(index, params.mode.getNormalized());

    // Processing: the block carries it, so no flush is asked for.
    check(host.host.flushRequests == 0);

    // While held, the host's own write is dropped.
    host.in.param(modeId, 0, 0.0);
    host.fillBlock(blockSize);
    host.run();
    check(params.mode.getValue() == 2.f);

    auto begins = host.out.ofType(CLAP_EVENT_PARAM_GESTURE_BEGIN);
    auto values = host.out.ofType(CLAP_EVENT_PARAM_VALUE);
    check(begins.size() == 1 && values.size() == 1);
    check(values.size() == 1 && values[0]->param.value == 2.0);
    check(host.out.ofType(CLAP_EVENT_PARAM_GESTURE_END).empty());

    listener.endParameterEdit(index);
    host.clearEvents();
    host.fillBlock(blockSize);
    host.run();
    check(host.out.ofType(CLAP_EVENT_PARAM_GESTURE_END).size() == 1);
    check(host.out.ofType(CLAP_EVENT_PARAM_VALUE).empty());
};

auto tPluginMoves = test("CLAP/aPluginSideMoveLeavesAValueEvent") = []
{
    auto host = PluginHost<MoveReportPlugin> {};
    auto& params = host.plugin().params;

    host.fillBlock(blockSize);
    host.run();
    check(host.out.events.empty());

    for (auto* param: {static_cast<Parameter*>(&params.plain),
                       static_cast<Parameter*>(&params.reported),
                       static_cast<Parameter*>(&params.unreported),
                       static_cast<Parameter*>(&params.quiet.muted),
                       static_cast<Parameter*>(&params.quiet.loud)})
        param->setNormalized(0.7f);

    host.clearEvents();
    host.fillBlock(blockSize);
    host.run();

    auto values = host.out.ofType(CLAP_EVENT_PARAM_VALUE);
    auto sent = [&](const Parameter& param)
    {
        auto id = host.idOf(param);
        return std::any_of(values.begin(),
                           values.end(),
                           [id](const AnyEvent* event)
                           { return event->param.param_id == id; });
    };

    check(values.size() == 3);
    check(sent(params.plain) && sent(params.reported) && sent(params.quiet.loud));
    check(!sent(params.unreported) && !sent(params.quiet.muted));

    for (const auto* event: values)
        check(event->header.time == 0 && near(event->param.value, 0.7));

    host.clearEvents();
    host.fillBlock(blockSize);
    host.run();
    check(host.out.events.empty());
};

auto tRescans = test("CLAP/infoAndValueChangesRescanTheHost") = []
{
    auto host = PluginHost<GainPlugin> {};
    host.host.clearRecords();

    host.plugin().notifyHostParameterInfoChanged();
    check(host.host.rescansWith(CLAP_PARAM_RESCAN_INFO) == 1);
    check(host.host.rescansWith(CLAP_PARAM_RESCAN_TEXT) == 1);

    host.plugin().notifyHostParameterValuesChanged();
    check(host.host.rescansWith(CLAP_PARAM_RESCAN_VALUES) == 1);
    check(host.host.rescansWith(CLAP_PARAM_RESCAN_ALL) == 0);

    host.plugin().notifyHostStateChanged();
    check(host.host.dirtyMarks == 1);
};

auto tState = test("CLAP/stateRoundTripsThroughTheStreams") = []
{
    auto source = PluginHost<GainPlugin> {};
    auto& params = source.plugin().params;
    params.gain.setValue(-12.f);
    params.mode.setValue(2.f);
    params.width.setValue(0.9f);

    auto document = source.save();
    check(!document.empty());

    auto target = PluginHost<GainPlugin> {};
    target.host.clearRecords();

    // In small reads, as a host may give them.
    auto stream = InStream {document, 7};
    check(target.state->load(target.clap, &stream.stream));

    auto& loaded = target.plugin().params;
    check(near(loaded.gain.getValue(), -12.f));
    check(loaded.mode.getValue() == 2.f);
    check(near(loaded.width.getValue(), 0.9f));
    check(target.host.rescansWith(CLAP_PARAM_RESCAN_VALUES) == 1);

    // Not echoed back as moves.
    target.fillBlock(blockSize);
    target.run();
    check(target.out.ofType(CLAP_EVENT_PARAM_VALUE).empty());

    // A stream that takes a few bytes at a time still gets all of them.
    auto partial = OutStream {};
    partial.maxWrite = 5;
    check(source.state->save(source.clap, &partial.stream));
    check(partial.data == document);
};

auto tStateContext = test("CLAP/aPresetContextLeavesSessionOnlyParametersOut") = []
{
    auto source = PluginHost<GainPlugin> {};
    source.plugin().params.width.setValue(0.9f);
    source.plugin().params.gain.setValue(-3.f);

    auto preset = source.save(CLAP_STATE_CONTEXT_FOR_PRESET);
    auto project = source.save(CLAP_STATE_CONTEXT_FOR_PROJECT);
    auto duplicate = source.save(CLAP_STATE_CONTEXT_FOR_DUPLICATE);
    check(project == duplicate && project == source.save());
    check(preset != project);

    auto target = PluginHost<GainPlugin> {};
    target.plugin().params.width.setValue(0.1f);
    check(target.load(preset, CLAP_STATE_CONTEXT_FOR_PRESET));
    check(near(target.plugin().params.gain.getValue(), -3.f));
    check(near(target.plugin().params.width.getValue(), 0.1f));

    check(target.load(project, CLAP_STATE_CONTEXT_FOR_PROJECT));
    check(near(target.plugin().params.width.getValue(), 0.9f));

    // An unknown context is refused.
    auto stream = OutStream {};
    check(!source.stateContext->save(source.clap, &stream.stream, 99));
};

auto tAudioPorts = test("CLAP/audioPortsFollowTheLayout") = []
{
    auto host = PluginHost<LatentEffect> {};
    const auto* ports = host.audioPorts;

    check(ports->count(host.clap, true) == 1);
    check(ports->count(host.clap, false) == 2);

    auto info = clap_audio_port_info {};
    check(ports->get(host.clap, 0, true, &info));
    check(info.id == 0 && info.channel_count == 2);
    check((info.flags & CLAP_AUDIO_PORT_IS_MAIN) != 0);
    check(std::string(info.port_type) == CLAP_PORT_STEREO);
    check(std::string(info.name) == "Input");
    check(info.in_place_pair == 0);

    check(ports->get(host.clap, 1, false, &info));
    check(info.id == 1 && std::string(info.name) == "Aux");
    check((info.flags & CLAP_AUDIO_PORT_IS_MAIN) == 0);
    check(info.in_place_pair == CLAP_INVALID_ID);
    check(!ports->get(host.clap, 2, false, &info));

    auto synth = PluginHost<SynthPlugin> {};
    check(synth.audioPorts->count(synth.clap, true) == 0);
    check(synth.audioPorts->get(synth.clap, 0, false, &info));
    check(info.in_place_pair == CLAP_INVALID_ID);
};

auto tNotePorts = test("CLAP/notePortsFollowTheMidiBuses") = []
{
    auto host = PluginHost<LatentEffect> {};
    const auto* ports = host.notePorts;

    check(ports->count(host.clap, true) == 1);
    check(ports->count(host.clap, false) == 1);

    auto info = clap_note_port_info {};
    check(ports->get(host.clap, 0, true, &info));
    check(info.id == 0 && std::string(info.name) == "MIDI In");
    check(info.supported_dialects
          == (CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI));
    check(info.preferred_dialect == CLAP_NOTE_DIALECT_MIDI);
    check(!ports->get(host.clap, 1, true, &info));

    auto gain = PluginHost<GainPlugin> {};
    check(gain.notePorts->count(gain.clap, true) == 0);
};

auto tLatency = test("CLAP/aLatencyChangeRestartsTheActiveHost") = []
{
    auto host = PluginHost<LatentEffect> {};
    auto& plugin = host.plugin();

    check(host.latency->get(host.clap) == 0);
    host.host.clearRecords();

    plugin.latency = 32;
    plugin.notifyHostLatencyChanged();
    check(host.host.restarts == 1);

    // Unchanged since: no second restart.
    plugin.notifyHostLatencyChanged();
    check(host.host.restarts == 1);

    // The host restarts: the activation tells it, and it reads the new value.
    check(host.configure(48000.0, maxBlock));
    check(host.host.latencyChanges == 1);
    check(host.latency->get(host.clap) == 32);

    // Moved while inactive: told during the activation.
    host.stop();
    plugin.latency = 64;
    plugin.notifyHostLatencyChanged();
    check(host.host.restarts == 1);
    check(host.configure(48000.0, maxBlock));
    check(host.host.latencyChanges == 2);
    check(host.latency->get(host.clap) == 64);

    // Unchanged across an activation: nothing to tell.
    check(host.configure(48000.0, maxBlock));
    check(host.host.latencyChanges == 2);

    // Moved under a block: a main-thread callback, which restarts.
    plugin.latency = 16;
    host.fillBlock(blockSize);
    host.run();
    check(host.host.callbacks == 1);
    host.clap->on_main_thread(host.clap);
    check(host.host.restarts == 2);
};

auto tTail = test("CLAP/theTailIsReportedInSamples") = []
{
    auto host = PluginHost<TailPlugin> {};
    check(host.tail->get(host.clap) == 0);

    host.plugin().tail = 4800;
    check(host.tail->get(host.clap) == 4800);

    host.plugin().tail = Plugin::infiniteTail;
    check(host.tail->get(host.clap) == std::numeric_limits<uint32_t>::max());
};

auto tRender = test("CLAP/anOfflineRenderReachesTheBlockAsOffline") = []
{
    auto host = PluginHost<GainPlugin> {};
    check(!host.render->has_hard_realtime_requirement(host.clap));

    check(host.render->set(host.clap, CLAP_RENDER_OFFLINE));
    host.fillBlock(blockSize);
    host.run();
    check(host.plugin().offline);

    check(host.render->set(host.clap, CLAP_RENDER_REALTIME));
    host.run();
    check(!host.plugin().offline);

    check(!host.render->set(host.clap, 7));
};

auto tAddedBypass = test("CLAP/theWrappersBypassIsListedAsTheBypass") = []
{
    auto host = PluginHost<LatentEffect> {};
    const auto& list = host.plugin().parameters();
    auto bypassIndex = list.bypassIndex();

    check(bypassIndex >= 0 && list.entry(bypassIndex).addedBypass);
    check(host.params->count(host.clap) == 2);

    auto bypassId = list.entry(bypassIndex).clapId;
    check(bypassId == hostIdFor("__bypass"));

    auto info = host.infoOf(bypassId);
    check((info.flags & CLAP_PARAM_IS_BYPASS) != 0);
    check((info.flags & CLAP_PARAM_IS_STEPPED) != 0);

    host.in.param(bypassId, 0, 1.0);

    for (auto block = 0; block < 4; ++block)
    {
        host.fillBlock(blockSize);
        host.run();
        host.clearEvents();
    }

    check(host.plugin().bypassed > 0);
};

auto tJuceBypass = test("CLAP/aJuceProfileListsNoAddedBypass") = []
{
    auto identity = policyFor(IdScheme::JUCE, {}).parameters;
    auto host = PluginHost<LatentEffect> {identity};
    const auto& list = host.plugin().parameters();

    // The wrapper still adds it (for VST3 and AAX), but CLAP never sees it.
    check(list.bypassIndex() >= 0);
    check(host.params->count(host.clap) == 1);
    check((host.info(0).flags & CLAP_PARAM_IS_BYPASS) == 0);

    // A declared bypass is listed and flagged under JUCE too.
    auto gain = PluginHost<GainPlugin> {identity};
    auto bypass = gain.infoOf(gain.idOf(gain.plugin().params.bypass));
    check((bypass.flags & CLAP_PARAM_IS_BYPASS) != 0);
};

auto tJuceIds = test("CLAP/aJucePortsIdsAreClapJuceExtensions") = []
{
    for (auto scheme: {IdScheme::JUCE, IdScheme::JUCELegacyIndex})
    {
        auto host = hostFor(JucePort::description(scheme));
        const auto& list = host.plugin().parameters();

        check(list.identityProblems().empty());
        check(host.params->count(host.clap) > 0);

        for (auto position = 0u; position < host.params->count(host.clap);
             ++position)
        {
            auto info = host.info(static_cast<int>(position));
            auto index = -1;

            for (auto i = 0; i < list.size(); ++i)
                if (list.entry(i).clapId == info.id)
                    index = i;

            check(index >= 0);

            if (index < 0)
                continue;

            const auto& entry = list.entry(index);

            // Unmasked, and the key's hash even where VST3's id is an index.
            check(info.id == Compat::juceStringHash(entry.hostKey));
            check(!entry.addedBypass);
            check(info.min_value == 0.0 && info.max_value == 1.0);
            check(((info.flags & CLAP_PARAM_IS_STEPPED) != 0)
                  == entry.param->isBoolean());
            check((info.flags & CLAP_PARAM_IS_ENUM) == 0);
            check(near(host.value(info.id), entry.param->getNormalized()));
        }
    }
};

auto tRules = test("CLAP/eachProfileResolvesItsClapRules") = []
{
    for (auto scheme: {IdScheme::Native, IdScheme::TamberPlug})
    {
        auto params = policyFor(scheme, {}).parameters;
        check(params.clapIds == ClapIdRule::HostId);
        check(params.clapValues == ClapValueSpace::Native);
        check(params.addedBypassInCLAP == BypassPlacement::Listed);
    }

    for (auto scheme: {IdScheme::JUCE, IdScheme::JUCELegacyIndex})
    {
        // Whatever JUCE's own settings say.
        auto params =
            policyFor(scheme, {.studioOneCompatibleParameters = false}).parameters;
        check(params.clapIds == ClapIdRule::JuceKeyHash);
        check(params.clapValues == ClapValueSpace::JuceNormalized);
        check(params.addedBypassInCLAP == BypassPlacement::Unlisted);
    }
};

// "Aa" and "BB" share String::hashCode, as JUCE would have found too.
struct CollidingParams : ParameterGroup
{
    CollidingParams() { add(first, second); }

    FloatParam first {"First", 0.f, 1.f, 0.f, {.id = "Aa", .versionHint = 1}};
    FloatParam second {"Second", 0.f, 1.f, 0.f, {.id = "BB", .versionHint = 1}};
};

auto tClapIdClash = test("CLAP/twoParametersSharingAClapIdAreAProblem") = []
{
    auto identity = policyFor(IdScheme::JUCE, {}).parameters;
    identity.juceChecks = false;

    auto params = CollidingParams {};
    auto list = ParameterList(params, identity);
    auto problems = list.identityProblems();

    check(list.entry(0).clapId == list.entry(1).clapId);
    check(std::any_of(problems.begin(),
                      problems.end(),
                      [](const std::string& problem)
                      { return problem.find("the CLAP id") != std::string::npos; }));
};

auto tNativeIds = test("CLAP/nativeIdsAreTheHostIds") = []
{
    auto host = PluginHost<GainPlugin> {};
    const auto& list = host.plugin().parameters();

    for (const auto& entry: list)
        check(entry.clapId == entry.hostId);

    auto plug =
        PluginHost<GainPlugin> {policyFor(IdScheme::TamberPlug, {}).parameters};

    for (const auto& entry: plug.plugin().parameters())
        check(entry.clapId == entry.hostId);
};

auto tMidi = test("CLAP/notesAndMidiReachThePluginAndEchoBack") = []
{
    auto host = PluginHost<SynthPlugin> {};

    host.in.note(CLAP_EVENT_NOTE_ON, 5, 60, 0.5, 2);
    host.in.midi(10, 0xB1, 74, 64);
    host.in.note(CLAP_EVENT_NOTE_CHOKE, 20, 60, 0.0, 2);
    host.in.note(CLAP_EVENT_NOTE_OFF, 30, -1);
    host.fillBlock(blockSize);
    host.run();

    const auto& plugin = host.plugin();
    check(plugin.numEvents == 3);
    check(plugin.events[0].isNoteOn() && plugin.events[0].sampleOffset == 5);
    check(plugin.events[0].channel == 2);
    check(plugin.events[0].asNoteOn()->pitch == 60);
    check(near(plugin.events[0].asNoteOn()->velocity, 0.5));
    check(plugin.events[1].isControlChange() && plugin.events[1].channel == 1);
    check(plugin.events[1].asControlChange()->controller == 74);
    check(plugin.events[2].isNoteOff() && plugin.events[2].sampleOffset == 20);

    auto midi = host.out.ofType(CLAP_EVENT_MIDI);
    check(midi.size() == 3);

    if (midi.size() == 3)
    {
        check(midi[0]->header.time == 5);
        check(midi[0]->midi.data[0] == 0x92 && midi[0]->midi.data[1] == 60);
        check(midi[1]->header.time == 10 && midi[1]->midi.data[0] == 0xB1);
        check(midi[1]->midi.data[1] == 74);
        check(midi[2]->header.time == 20 && (midi[2]->midi.data[0] & 0xf0) == 0x80);
        check(midi[0]->midi.port_index == 0);
    }
};

auto tSysEx = test("CLAP/aSysExRoundTripsAndAnOversizedOneIsDropped") = []
{
    auto host = PluginHost<SynthPlugin> {};
    auto bytes = std::array<uint8_t, 6> {0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7};
    auto large = std::array<uint8_t, 40> {};
    large.front() = 0xF0;
    large.back() = 0xF7;

    host.in.sysEx(3, bytes.data(), static_cast<uint32_t>(bytes.size()));
    host.in.sysEx(4, large.data(), static_cast<uint32_t>(large.size()));
    host.fillBlock(blockSize);
    host.run();

    check(host.plugin().numEvents == 1);
    const auto* sysEx = host.plugin().events[0].asSysEx();
    check(sysEx != nullptr && sysEx->size == 6);
    check(sysEx != nullptr
          && std::equal(bytes.begin(), bytes.end(), sysEx->data.begin()));

    auto out = host.out.ofType(CLAP_EVENT_MIDI_SYSEX);
    check(out.size() == 1);

    if (out.size() == 1)
    {
        check(out[0]->header.time == 3 && out[0]->sysEx.size == 6);
        check(std::equal(bytes.begin(), bytes.end(), out[0]->sysEx.buffer));
    }
};

auto tTransport = test("CLAP/theTransportArrivesAsThePlayhead") = []
{
    auto host = PluginHost<GainPlugin> {};

    host.transport.header =
        headerOf(sizeof(clap_event_transport), CLAP_EVENT_TRANSPORT, 0);
    host.transport.flags =
        CLAP_TRANSPORT_HAS_TEMPO | CLAP_TRANSPORT_HAS_BEATS_TIMELINE
        | CLAP_TRANSPORT_HAS_SECONDS_TIMELINE | CLAP_TRANSPORT_HAS_TIME_SIGNATURE
        | CLAP_TRANSPORT_IS_PLAYING | CLAP_TRANSPORT_IS_LOOP_ACTIVE;
    host.transport.tempo = 140.0;
    host.transport.song_pos_beats = 6 * CLAP_BEATTIME_FACTOR;
    host.transport.bar_start = 4 * CLAP_BEATTIME_FACTOR;
    host.transport.song_pos_seconds = 2 * CLAP_SECTIME_FACTOR;
    host.transport.loop_start_beats = 1 * CLAP_BEATTIME_FACTOR;
    host.transport.loop_end_beats = 9 * CLAP_BEATTIME_FACTOR;
    host.transport.tsig_num = 3;
    host.transport.tsig_denom = 8;
    host.useTransport = true;

    host.fillBlock(blockSize);
    host.run();

    const auto& playhead = host.plugin().playhead;
    check(playhead.isValid && playhead.isPlaying && playhead.isLooping);
    check(!playhead.isRecording);
    check(near(playhead.bpm, 140.0));
    check(near(playhead.ppqPosition, 6.0));
    check(near(playhead.barStartPpq, 4.0));
    check(near(playhead.loopStartPpq, 1.0) && near(playhead.loopEndPpq, 9.0));
    check(playhead.sampleTime == 96000);
    check(playhead.timeSignature.numerator == 3);
    check(playhead.timeSignature.denominator == 8);

    // No transport: no playhead.
    host.useTransport = false;
    host.fillBlock(blockSize);
    host.run();
    check(!host.plugin().playhead.isValid);
};

auto tTrackInfo = test("CLAP/theHostsTrackInfoReachesThePlugin") = []
{
    auto host = PluginHost<GainPlugin> {};
    check(host.plugin().trackInfoChanges == 0);

    auto info = clap_track_info {};
    info.flags = CLAP_TRACK_INFO_HAS_TRACK_NAME | CLAP_TRACK_INFO_HAS_TRACK_COLOR;
    std::strcpy(info.name, "Lead Vocal");
    info.color = {255, 10, 20, 30};
    host.host.track = info;

    host.trackInfo->changed(host.clap);

    const auto& plugin = host.plugin();
    check(plugin.trackInfoChanges == 1);
    check(plugin.trackInfo().isValid);
    check(plugin.trackInfo().name == "Lead Vocal");
    check(plugin.trackInfo().color == TrackColor {10, 20, 30, 255});
};

auto tNoGui = test("CLAP/noGuiExtensionIsOfferedYet") = []
{
    auto host = PluginHost<GainPlugin> {};
    check(host.extension<void>(CLAP_EXT_GUI) == nullptr);
    check(host.extension<void>("clap.unknown") == nullptr);
    check(host.extension<void>(CLAP_EXT_TRACK_INFO_COMPAT) != nullptr);
};

// One pass of the host's loop after another until `ready`, for a second or so.
template <typename Predicate>
bool pumpUntil(Predicate ready, const std::function<void()>& pass)
{
    for (auto attempt = 0; attempt < 200 && !ready(); ++attempt)
    {
        pass();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    return ready();
}

// Where the message loop has a descriptor (Linux) the host's loop pumps it:
// through posix-fd-support, or a timer where the host has none, each removed at
// destroy. Elsewhere the platform's own loop serves and nothing is registered.
auto tHostLoop = test("CLAP/theHostsLoopPumpsTheMessageLoop") = []
{
    auto fd = messageLoopFd();

    for (auto offersPosixFd: {true, false})
    {
        auto testHost = TestHost {};
        testHost.offersPosixFd = offersPosixFd;

        auto* adapter = new MakeASound::CLAP::Adapter(
            &testHost.host, &testDescriptor(), EA::makeOwned<GainPlugin>());
        const auto* clap = adapter->clapPlugin();
        check(clap->init(clap));

        const auto* fdSupport = static_cast<const clap_plugin_posix_fd_support*>(
            clap->get_extension(clap, CLAP_EXT_POSIX_FD_SUPPORT));
        const auto* timerSupport = static_cast<const clap_plugin_timer_support*>(
            clap->get_extension(clap, CLAP_EXT_TIMER_SUPPORT));

        if (fd < 0)
        {
            check(testHost.fds.empty() && testHost.timerPeriods.empty());
            check(fdSupport == nullptr && timerSupport == nullptr);
        }
        else
        {
            auto ran = std::make_shared<bool>(false);
            callOnMessageThread([ran] { *ran = true; });

            if (offersPosixFd)
            {
                check(testHost.fds.size() == 1 && testHost.timerPeriods.empty());
                check(!testHost.fds.empty() && testHost.fds[0].first == fd);
                check(!testHost.fds.empty()
                      && (testHost.fds[0].second & CLAP_POSIX_FD_READ) != 0);
                check(fdSupport != nullptr);

                if (fdSupport != nullptr)
                    check(pumpUntil(
                        [&] { return *ran; },
                        [&] { fdSupport->on_fd(clap, fd, CLAP_POSIX_FD_READ); }));
            }
            else
            {
                check(testHost.fds.empty() && testHost.timerPeriods.size() == 1);
                check(!testHost.timerPeriods.empty()
                      && testHost.timerPeriods[0].second == 16);
                check(timerSupport != nullptr);

                if (timerSupport != nullptr && !testHost.timerPeriods.empty())
                {
                    auto timer = testHost.timerPeriods[0].first;
                    check(pumpUntil([&] { return *ran; },
                                    [&] { timerSupport->on_timer(clap, timer); }));
                }
            }
        }

        clap->destroy(clap);
        check(testHost.fds.empty() && testHost.timerPeriods.empty());
    }
};

// A host whose list takes one event per drain: nothing the plugin edited is
// lost, and the gesture still closes, in order.
auto tGestureRefused = test("CLAP/aGestureTheHostRefusedLeavesAtTheNextDrain") = []
{
    auto host = PluginHost<GainPlugin> {};
    auto& params = host.plugin().params;
    auto& listener = *host.plugin().hostEditListener();
    auto index = host.plugin().parameters().indexOf(&params.gain);

    host.stop();
    listener.beginParameterEdit(index);
    params.gain.setNormalized(0.25f);
    listener.performParameterEdit(index, 0.25f);
    listener.endParameterEdit(index);

    auto types = std::vector<uint16_t> {};
    host.out.limit = 1;

    for (auto drain = 0; drain < 5; ++drain)
    {
        host.clearEvents();
        host.flush();

        for (const auto& event: host.out.events)
            types.push_back(event.header.type);
    }

    check(types
          == std::vector<uint16_t> {CLAP_EVENT_PARAM_GESTURE_BEGIN,
                                    CLAP_EVENT_PARAM_VALUE,
                                    CLAP_EVENT_PARAM_GESTURE_END});
};

// More edits than any queue holds: the values coalesce into the latest, and
// the gesture's end still leaves.
auto tManyEdits = test("CLAP/aLongGestureStillEndsAndSendsItsLatestValue") = []
{
    auto host = PluginHost<GainPlugin> {};
    auto& listener = *host.plugin().hostEditListener();
    auto index = host.plugin().parameters().indexOf(&host.plugin().params.gain);

    host.stop();
    listener.beginParameterEdit(index);

    for (auto i = 0; i < 5000; ++i)
        listener.performParameterEdit(index, static_cast<float>(i % 100) / 100.f);

    host.plugin().params.gain.setNormalized(0.75f);
    listener.performParameterEdit(index, 0.75f);
    listener.endParameterEdit(index);
    host.flush();

    const auto& events = host.out.events;
    check(events.size() == 3);

    if (events.size() == 3)
    {
        check(events[0].header.type == CLAP_EVENT_PARAM_GESTURE_BEGIN);
        check(events[1].header.type == CLAP_EVENT_PARAM_VALUE);
        check(near(events[1].param.value, 0.75));
        check(events[2].header.type == CLAP_EVENT_PARAM_GESTURE_END);
    }
};

auto tNestedGesture = test("CLAP/aNestedGestureBeginsAndEndsOnce") = []
{
    auto host = PluginHost<GainPlugin> {};
    auto& listener = *host.plugin().hostEditListener();
    auto index = host.plugin().parameters().indexOf(&host.plugin().params.gain);

    listener.beginParameterEdit(index);
    listener.beginParameterEdit(index);
    listener.performParameterEdit(index, 0.5f);
    listener.endParameterEdit(index);

    host.fillBlock(blockSize);
    host.run();
    check(host.out.ofType(CLAP_EVENT_PARAM_GESTURE_BEGIN).size() == 1);
    check(host.out.ofType(CLAP_EVENT_PARAM_VALUE).size() == 1);
    check(host.out.ofType(CLAP_EVENT_PARAM_GESTURE_END).empty());
    check(host.adapter().wrapper().isParameterHeld(index));

    listener.beginParameterEdit(index);
    listener.endParameterEdit(index);
    listener.endParameterEdit(index);

    host.clearEvents();
    host.fillBlock(blockSize);
    host.run();
    check(host.out.ofType(CLAP_EVENT_PARAM_GESTURE_BEGIN).empty());
    check(host.out.ofType(CLAP_EVENT_PARAM_GESTURE_END).size() == 1);
    check(!host.adapter().wrapper().isParameterHeld(index));
};

// An edit the last block missed: stop_processing (the audio thread) asks for a
// main-thread callback, which asks for the flush that sends it.
auto tStopProcessing = test("CLAP/anEditLeftAtStopProcessingIsFlushedLater") = []
{
    auto host = PluginHost<GainPlugin> {};
    auto& listener = *host.plugin().hostEditListener();
    auto index = host.plugin().parameters().indexOf(&host.plugin().params.gain);

    host.host.clearRecords();
    listener.beginParameterEdit(index);
    host.plugin().params.gain.setNormalized(0.5f);
    listener.performParameterEdit(index, 0.5f);
    listener.endParameterEdit(index);
    check(host.host.flushRequests == 0);

    host.clap->stop_processing(host.clap);
    host.processing = false;
    check(host.host.callbacks == 1);
    check(host.host.flushRequests == 0);

    host.clap->on_main_thread(host.clap);
    check(host.host.flushRequests == 1);

    host.flush();
    check(host.out.events.size() == 3);

    // Nothing left: no second callback.
    host.processing = host.clap->start_processing(host.clap);
    host.clap->stop_processing(host.clap);
    host.processing = false;
    check(host.host.callbacks == 1);
};

// An IntParam whose range the plugin moves, as one following a sample count
// or a table size might.
struct ElasticParam : IntParam
{
    using IntParam::IntParam;

    float maxValue() const noexcept override { return static_cast<float>(top); }
    int numSteps() const noexcept override { return top; }

    int top = 4;
};

struct ElasticParams : ParameterGroup
{
    ElasticParams() { add(steps); }

    ElasticParam steps {"Steps", 0, 4, 0, {.versionHint = 1}};
};

struct ElasticPlugin : StatePlugin<State<ElasticParams>>
{
    std::string_view name() const override { return "Elastic"; }

    void prepare(const ProcessSpec&) override {}
    void process(ProcessContext&) noexcept override {}
};

auto tRangeChange = test("CLAP/aRangeChangeRescansEverythingWhileInactive") = []
{
    auto host = PluginHost<ElasticPlugin> {};
    auto& steps = host.plugin().params.steps;
    auto id = host.idOf(steps);

    check(host.infoOf(id).max_value == 4.0);
    check((host.infoOf(id).flags & CLAP_PARAM_IS_STEPPED) != 0);
    host.host.clearRecords();

    // Active: a restart, and the host keeps the range it has until then.
    steps.top = 8;
    host.plugin().notifyHostParameterInfoChanged();
    check(host.host.restarts == 1);
    check(host.host.rescansWith(CLAP_PARAM_RESCAN_ALL) == 0);
    check(host.infoOf(id).max_value == 4.0);

    host.plugin().notifyHostParameterInfoChanged();
    check(host.host.restarts == 1);

    // Rebuilt at deactivation, and rescanned whole.
    host.stop();
    check(host.host.rescansWith(CLAP_PARAM_RESCAN_ALL) == 1);
    check(host.infoOf(id).max_value == 8.0);

    // Inactive: at once.
    steps.top = 2;
    host.plugin().notifyHostParameterInfoChanged();
    check(host.host.rescansWith(CLAP_PARAM_RESCAN_ALL) == 2);
    check(host.infoOf(id).max_value == 2.0);
    check(host.host.restarts == 1);

    // Unchanged: names and text only.
    auto infos = host.host.rescansWith(CLAP_PARAM_RESCAN_INFO);
    host.plugin().notifyHostParameterInfoChanged();
    check(host.host.rescansWith(CLAP_PARAM_RESCAN_ALL) == 2);
    check(host.host.rescansWith(CLAP_PARAM_RESCAN_INFO) == infos + 1);
};

// A plugin writing its MIDI out of order.
struct UnsortedMidiPlugin : SynthPlugin
{
    void process(ProcessContext& context) noexcept override
    {
        auto& out = context.mainMidiOut();
        out.add(MIDI::Event::noteOn(0, 60, 0.5f, 30));
        out.add(MIDI::Event::noteOn(0, 62, 0.5f, 10));
        out.add(MIDI::Event::noteOff(0, 64, 0.f, 20));
        out.add(MIDI::Event::noteOn(0, 64, 0.5f, 20));
    }
};

auto tMidiOrder = test("CLAP/midiOutLeavesInTimeOrder") = []
{
    auto host = PluginHost<UnsortedMidiPlugin> {};
    host.fillBlock(blockSize);
    host.run();

    auto midi = host.out.ofType(CLAP_EVENT_MIDI);
    check(midi.size() == 4);

    if (midi.size() == 4)
    {
        check(midi[0]->header.time == 10 && midi[1]->header.time == 20);
        check(midi[2]->header.time == 20 && midi[3]->header.time == 30);

        // Stable: the note-off stays ahead of the note-on at its offset.
        check((midi[1]->midi.data[0] & 0xf0) == 0x80);
        check((midi[2]->midi.data[0] & 0xf0) == 0x90);
    }
};
} // namespace
} // namespace CLAPTests
