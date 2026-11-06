// Hosting VST3 plugins: the fixture plugins (Tests/HostFixtures) loaded by class
// id through the FormatManager, their buses and parameters (ids, text through the
// controller, renames, latency), the begin/perform/end of the plugin's own GUI
// reaching the listener, output parameters, MIDI both ways, the state round trip,
// the fallback playhead and buses the caller does not supply. Malformed text, from
// a plugin (its classes, buses and parameters) or typed by a user, never throws.

#include <MakeASound/Plugin/VST3/VST3Common.h>

#include <MakeASound/Hosting/MakeASoundHosting.h>
#include <MakeASound/Hosting/VST3/VST3HostedPlugin.h>
#include <MakeASound/Hosting/VST3/VST3Classes.h>

#include <base/source/fobject.h>
#include <pluginterfaces/base/ibstream.h>

#include <NanoTest/NanoTest.h>

#include <algorithm>
#include <array>
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

// A dual-component plugin by hand, for what the fixtures cannot show (they are
// MakeASound's single-component adapter, whose controller keeps no state and
// hides only its MIDI shadow parameters, last): a controller with state of its
// own and hidden parameters between visible ones. No processor, so it never
// prepares; enough for state and parameters.
namespace Stub
{
using namespace Steinberg;
using namespace Steinberg::Vst;

std::string readAll(IBStream* stream)
{
    auto bytes = std::string {};
    char chunk[256];
    auto read = int32 {};

    while (stream != nullptr && stream->read(chunk, sizeof chunk, &read) == kResultOk
           && read > 0)
        bytes.append(chunk, static_cast<size_t>(read));

    return bytes;
}

tresult writeAll(IBStream* stream, const std::string& bytes)
{
    if (stream == nullptr)
        return kInvalidArgument;

    auto written = int32 {};
    return stream->write(
        const_cast<char*>(bytes.data()), static_cast<int32>(bytes.size()), &written);
}

struct Component
    : public FObject
    , public IComponent
{
    tresult PLUGIN_API initialize(FUnknown*) override { return kResultOk; }
    tresult PLUGIN_API terminate() override { return kResultOk; }
    tresult PLUGIN_API getControllerClassId(TUID) override
    {
        return kNotImplemented;
    }
    tresult PLUGIN_API setIoMode(IoMode) override { return kResultOk; }
    int32 PLUGIN_API getBusCount(MediaType, BusDirection) override { return 0; }

    tresult PLUGIN_API getBusInfo(MediaType, BusDirection, int32, BusInfo&) override
    {
        return kInvalidArgument;
    }

    tresult PLUGIN_API getRoutingInfo(RoutingInfo&, RoutingInfo&) override
    {
        return kNotImplemented;
    }

    tresult PLUGIN_API activateBus(MediaType, BusDirection, int32, TBool) override
    {
        return kResultOk;
    }

    tresult PLUGIN_API setActive(TBool) override { return kResultOk; }

    tresult PLUGIN_API setState(IBStream* stream) override
    {
        state = readAll(stream);
        return kResultOk;
    }

    tresult PLUGIN_API getState(IBStream* stream) override
    {
        return writeAll(stream, state);
    }

    std::string state = "component";

    // clang-format off
    OBJ_METHODS(Component, FObject)
    REFCOUNT_METHODS(FObject)
    DEFINE_INTERFACES
        DEF_INTERFACE(IComponent)
    END_DEFINE_INTERFACES(FObject)
    // clang-format on
};

// Controller parameters, in getParameterInfo order (ids 100..108):
//
//   0 Hidden A    hidden          5 Grouped X   unit 1
//   1 Visible A                   6 Root Late
//   2 Hidden B    hidden          7 Grouped Y   unit 1
//   3 Visible B                   8 Nested Z    unit 2, inside unit 1
//   4 Hidden C    hidden
//
// so JUCE's host, which grouped by unit, listed them 0-5, 7, 8, 6. Its own state
// is a byte string; loading one also moves "Visible B" to 0.25, so a host that
// re-reads values sees it.
struct Controller : EditControllerEx1
{
    Controller()
    {
        addUnit(new Unit(u"Root", kRootUnitId, kNoParentUnitId));
        addUnit(new Unit(u"One", 1, kRootUnitId));
        addUnit(new Unit(u"Two", 2, 1));

        auto add =
            [this](const char16* title, ParamID id, bool hidden, UnitID unit = 0)
        {
            parameters.addParameter(title,
                                    nullptr,
                                    0,
                                    0.5,
                                    hidden ? ParameterInfo::kIsHidden : 0,
                                    id,
                                    unit);
        };

        add(u"Hidden A", 100, true);
        add(u"Visible A", 101, false);
        add(u"Hidden B", 102, true);
        add(u"Visible B", 103, false);
        add(u"Hidden C", 104, true);
        add(u"Grouped X", 105, false, 1);
        add(u"Root Late", 106, false);
        add(u"Grouped Y", 107, false, 1);
        add(u"Nested Z", 108, false, 2);
    }

    // IEditController's getState/setState, renamed by VST3Common.h.
    tresult PLUGIN_API setEditorState(IBStream* stream) override
    {
        controllerState = readAll(stream);
        ++loads;
        setParamNormalized(103, 0.25);
        return kResultOk;
    }

    tresult PLUGIN_API getEditorState(IBStream* stream) override
    {
        if (controllerState.empty())
            return kResultFalse;

        return writeAll(stream, controllerState);
    }

    tresult PLUGIN_API setComponentState(IBStream* stream) override
    {
        componentState = readAll(stream);
        return kResultOk;
    }

    std::string controllerState;
    std::string componentState;
    int loads = 0;
};

struct Loaded
{
    Loaded()
        : component(owned(new Component()))
        , controller(owned(new Controller()))
        , plugin(MakeASound::Hosting::PluginDescription {},
                 nullptr,
                 nullptr,
                 IPtr<IComponent>(component.get()),
                 IPtr<IEditController>(controller.get()),
                 true)
    {
    }

    IPtr<Component> component;
    IPtr<Controller> controller;
    VST3HostedPlugin plugin;
};

// Text a plugin can hand a host that the SDK's StringConvert cannot read: a lone
// surrogate throws std::range_error there, a character past U+FFFF throws where it
// converts one unit at a time (a class's PClassInfoW), and a full String128 with no
// terminator reads past its end.
constexpr auto high = char16_t {0xd83d};
constexpr auto low = char16_t {0xde00};

// U+FFFD, what each broken sequence reads as, and U+1F600, the pair above.
const auto replacement = std::string {"\xEF\xBF\xBD"};
const auto smile = std::string {"\xF0\x9F\x98\x80"};

// Exactly these units, terminated only when they leave room for it.
template <size_t Size>
void setUnits(TChar (&target)[Size], const std::u16string& units)
{
    std::fill(std::begin(target), std::end(target), TChar {0});
    std::copy_n(units.data(), std::min(units.size(), Size), target);
}

template <size_t Size>
void setBytes(char8 (&target)[Size], std::string_view bytes)
{
    std::fill(std::begin(target), std::end(target), char8 {0});
    std::copy_n(bytes.data(), std::min(bytes.size(), Size), target);
}

// An audio bus each way and a MIDI input, each name broken its own way.
struct BadTextComponent : Component
{
    int32 PLUGIN_API getBusCount(MediaType type, BusDirection direction) override
    {
        return type == kAudio || direction == kInput ? 1 : 0;
    }

    tresult PLUGIN_API getBusInfo(MediaType type,
                                  BusDirection direction,
                                  int32 index,
                                  BusInfo& info) override
    {
        if (index != 0)
            return kInvalidArgument;

        info = BusInfo {};
        info.mediaType = type;
        info.direction = direction;
        info.channelCount = type == kAudio ? 2 : 16;
        info.busType = kMain;
        info.flags = BusInfo::kDefaultActive;

        if (type == kEvent)
            setUnits(info.name, {low, u'M'});
        else if (direction == kInput)
            setUnits(info.name, {u'I', u'n', high});
        else
            setUnits(info.name, std::u16string(128, u'o'));

        return kResultOk;
    }
};

// One parameter, its title, short title and text broken, and every string it
// is asked to parse kept.
struct BadTextController : EditController
{
    int32 PLUGIN_API getParameterCount() override { return 1; }

    tresult PLUGIN_API getParameterInfo(int32 index, ParameterInfo& info) override
    {
        if (index != 0)
            return kInvalidArgument;

        info = ParameterInfo {};
        info.id = 7;
        info.defaultNormalizedValue = 0.5;
        info.flags = ParameterInfo::kCanAutomate;
        setUnits(info.title, {u'G', u'a', u'i', u'n', high});
        setUnits(info.shortTitle, std::u16string(127, u's') + high);
        setUnits(info.units, {high, low});
        return kResultOk;
    }

    tresult PLUGIN_API getParamStringByValue(ParamID,
                                             ParamValue,
                                             String128 text) override
    {
        const auto units = std::u16string {u'5', low, u'%'};
        std::copy(units.begin(), units.end(), text);
        text[units.size()] = 0;
        return kResultOk;
    }

    tresult PLUGIN_API getParamValueByString(ParamID,
                                             TChar* text,
                                             ParamValue& normalized) override
    {
        parsed.add(std::u16string(text));
        normalized = 0.25;
        return kResultOk;
    }

    Vector<std::u16string> parsed;
};

struct BadTextLoaded
{
    BadTextLoaded()
        : component(owned(new BadTextComponent()))
        , controller(owned(new BadTextController()))
        , plugin(MakeASound::Hosting::PluginDescription {},
                 nullptr,
                 nullptr,
                 IPtr<IComponent>(component.get()),
                 IPtr<IEditController>(controller.get()),
                 true)
    {
    }

    IPtr<BadTextComponent> component;
    IPtr<BadTextController> controller;
    VST3HostedPlugin plugin;
};

// A module's factory: class 0 is unicode (IPluginFactory3) with a name past
// U+FFFF, a vendor ending in a lone surrogate and a version with no terminator;
// class 1 is plain (PClassInfo2) with no vendor of its own; class 2 it cannot
// describe at all.
struct BadTextFactory
    : public FObject
    , public IPluginFactory3
{
    static constexpr TUID unicodeId = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    static constexpr TUID plainId = {2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24};

    tresult PLUGIN_API getFactoryInfo(PFactoryInfo* info) override
    {
        *info = PFactoryInfo("Stub Vendor", "", "", PFactoryInfo::kUnicode);
        return kResultOk;
    }

    int32 PLUGIN_API countClasses() override { return 3; }

    tresult PLUGIN_API getClassInfo(int32, PClassInfo*) override
    {
        return kResultFalse;
    }

    tresult PLUGIN_API createInstance(FIDString, FIDString, void** object) override
    {
        *object = nullptr;
        return kNotImplemented;
    }

    tresult PLUGIN_API getClassInfo2(int32 index, PClassInfo2* info) override
    {
        if (index != 1)
            return kResultFalse;

        *info = PClassInfo2(plainId,
                            PClassInfo::kManyInstances,
                            kVstAudioEffectClass,
                            "Plain",
                            0,
                            "Fx",
                            "",
                            "1.0",
                            "VST 3.7");
        return kResultTrue;
    }

    tresult PLUGIN_API getClassInfoUnicode(int32 index, PClassInfoW* info) override
    {
        if (index != 0)
            return kResultFalse;

        *info = PClassInfoW {};
        std::copy_n(unicodeId, sizeof(TUID), info->cid);
        info->cardinality = PClassInfo::kManyInstances;
        setBytes(info->category, kVstAudioEffectClass);
        setUnits(info->name, {u'S', u'm', u'i', u'l', u'e', u' ', high, low});
        setUnits(info->vendor, {u'V', high});
        setUnits(info->version, std::u16string(PClassInfo2::kVersionSize, u'1'));
        setUnits(info->sdkVersion, u"VST 3.7");
        setBytes(info->subCategories, "Fx|Delay");
        return kResultTrue;
    }

    tresult PLUGIN_API setHostContext(FUnknown*) override { return kResultOk; }

    // clang-format off
    OBJ_METHODS(BadTextFactory, FObject)
    REFCOUNT_METHODS(FObject)
    DEFINE_INTERFACES
        DEF_INTERFACE(IPluginFactory)
        DEF_INTERFACE(IPluginFactory2)
        DEF_INTERFACE(IPluginFactory3)
    END_DEFINE_INTERFACES(FObject)
    // clang-format on
};
} // namespace Stub

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
    check(list[cutoff].minValue() == 0.f && list[cutoff].maxValue() == 1.f);

    // Text through the plugin's controller: its skew and unit. The unit is in
    // the text alone: the fixture publishes no VST3 units, so no label here.
    auto text = list[cutoff].valueToText(list[cutoff].getValue());
    check(list[cutoff].label().empty());
    check(text.find("1000") != std::string::npos);
    check(text.find("Hz") != std::string::npos);
    check(text.find("Hz") == text.rfind("Hz"));

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

auto tTail = test("HostedVST3/aTailIsReadFiniteOrInfinite") = []
{
    const auto formats = DefaultFormats();

    check(load(formats, "HostFixtureEffect")->tailSamples() == 4800);
    check(load(formats, "HostFixtureInstrument")->tailSamples()
          == Plugin::infiniteTail);
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

auto tSysEx = test("HostedVST3/sysExGoesInAndComesBackOut") = []
{
    const auto formats = DefaultFormats();
    auto plugin = load(formats, "HostFixtureInstrument");
    auto block = Block(*plugin);

    const auto shortOne =
        std::array<uint8_t, 6> {0xf0, 0x41, 0x10, 0x42, 0x12, 0xf7};
    auto longest = std::array<uint8_t, MIDI::SysEx::maxBytes> {};
    longest.front() = 0xf0;

    for (auto i = size_t {1}; i + 1 < longest.size(); ++i)
        longest[i] = static_cast<uint8_t>(i);

    longest.back() = 0xf7;

    block.context.midiIn[0].add(
        MIDI::Event::sysEx(shortOne.data(), static_cast<int>(shortOne.size()), 5));
    block.context.midiIn[0].add(MIDI::Event::noteOn(0, 64, 0.5f, 6));
    block.context.midiIn[0].add(
        MIDI::Event::sysEx(longest.data(), static_cast<int>(longest.size()), 9));
    block.run();
    block.context.midiIn[0].clear();

    // In order, bytes and offsets intact, F0 to F7.
    const auto& out = block.context.midiOut[0];
    check(out.size() == 3);

    if (out.size() != 3)
        return;

    const auto* first = out[0].asSysEx();
    check(first != nullptr && out[0].sampleOffset == 5);
    check(first != nullptr && first->size == 6
          && std::equal(shortOne.begin(), shortOne.end(), first->data.begin()));

    check(out[1].isNoteOn());

    const auto* last = out[2].asSysEx();
    check(last != nullptr && out[2].sampleOffset == 9);
    check(last != nullptr && last->size == MIDI::SysEx::maxBytes
          && last->data == longest);
};

auto tControllerState =
    test("HostedVST3/theControllersOwnStateRoundTripsAfterTheComponents") = []
{
    auto source = Stub::Loaded();
    source.controller->controllerState = std::string {"ctl\0state", 9};

    // Apart from the component's: saveState is the component's alone.
    check(source.plugin.saveState(StateContext::Session) == "component");
    check(source.plugin.saveControllerState() == std::string {"ctl\0state", 9});

    auto copy = Stub::Loaded();
    auto visibleB = 1;
    check(copy.plugin.parameters()[visibleB].name() == "Visible B");
    check(copy.plugin.parameters()[visibleB].getValue() == 0.5f);

    // JUCE's order: the component, the controller told it, then the
    // controller's own, and the values re-read.
    copy.plugin.loadState(source.plugin.saveState(StateContext::Session),
                          StateContext::Session);
    copy.plugin.loadControllerState(source.plugin.saveControllerState());

    check(copy.component->state == "component");
    check(copy.controller->componentState == "component");
    check(copy.controller->controllerState == std::string {"ctl\0state", 9});
    check(copy.controller->loads == 1);
    check(copy.plugin.parameters()[visibleB].getValue() == 0.25f);

    // Nothing to load is not a load.
    copy.plugin.loadControllerState({});
    check(copy.controller->loads == 1);

    // A controller that saves nothing (kResultFalse) saves empty.
    auto silent = Stub::Loaded();
    check(silent.plugin.saveControllerState().empty());
};

auto tLegacyIndices = test("HostedVST3/aJuceIndexFindsTheSameParameter") = []
{
    auto loaded = Stub::Loaded();
    const auto& list = loaded.plugin.parameters();

    // The hidden ones are left out here, and the rest keep getParameterInfo's
    // order.
    check(list.size() == 6);

    auto nameAt = [&](int32_t juceIndex)
    {
        auto index = loaded.plugin.indexForControllerIndex(juceIndex);
        return index >= 0 ? std::string {list[index].name()} : std::string {"-"};
    };

    check(nameAt(0) == "-");
    check(nameAt(1) == "Visible A");
    check(nameAt(2) == "-");
    check(nameAt(3) == "Visible B");
    check(nameAt(4) == "-");
    check(nameAt(5) == "Grouped X");
    check(nameAt(6) == "Grouped Y");
    check(nameAt(7) == "Nested Z");
    check(nameAt(8) == "Root Late");
    check(nameAt(9) == "-");
    check(nameAt(-1) == "-");
};

auto tFixtureIndices = test("HostedVST3/aFixturesJuceIndicesAreItsOwn") = []
{
    // MakeASound's adapter lists its own parameters first, in one unit tree
    // order, and hides only its MIDI shadow parameters, after them.
    const auto formats = DefaultFormats();

    for (auto name: {"HostFixtureEffect", "HostFixtureInstrument"})
    {
        auto plugin = load(formats, name);
        auto count = plugin->parameters().size();
        check(count > 0);

        for (auto i = 0; i < count; ++i)
            check(plugin->indexForControllerIndex(i) == i);

        for (auto i = count; i < count + 16; ++i)
            check(plugin->indexForControllerIndex(i) == -1);
    }
};

auto tFixtureControllerState =
    test("HostedVST3/aControllerWithoutStateSavesNothing") = []
{
    // MakeASound's adapter is one object with no controller state of its own.
    const auto formats = DefaultFormats();
    auto plugin = load(formats, "HostFixtureEffect");
    auto cutoff = indexNamed(*plugin, "Cutoff");
    plugin->parameters()[cutoff].setValue(0.3f);

    check(plugin->saveControllerState().empty());
    plugin->loadControllerState("anything");
    check(near(plugin->parameters()[cutoff].getValue(), 0.3f));
};

auto tTypedText = test("HostedVST3/malformedTextAUserTypesNeverThrows") = []
{
    const auto formats = DefaultFormats();
    auto plugin = load(formats, "HostFixtureEffect");
    const auto& cutoff = plugin->parameters()[indexNamed(*plugin, "Cutoff")];

    // A lone lead byte, bytes no UTF-8 has, a cut-off four-byte sequence, an
    // encoded surrogate, and a broken byte inside text the plugin would parse.
    for (auto typed: {std::string {"\xC3"},
                      std::string {"\xFF\xFE"},
                      std::string {"\xF0\x9F\x98"},
                      std::string {"\xED\xA0\x80"},
                      std::string {"1000\xC3 Hz"}})
    {
        auto value = cutoff.textToValue(typed);
        check(std::isfinite(value) && value >= 0.f && value <= 1.f);
    }

    // Well-formed text still reaches the plugin's own parse.
    auto value = cutoff.getValue();
    check(near(cutoff.textToValue(cutoff.valueToText(value)), value, 1e-3f));
};

auto tHostName = test("HostedVST3/anyHostNameLoads") = []
{
    // The fixture asks the host's name as it initializes: one that is not UTF-8
    // reads with U+FFFD in it, and one past a String128 is cut to fit.
    const auto formats = DefaultFormats();
    const auto description = describe(formats, "HostFixtureEffect");

    for (auto name: {std::string {"Host \xFF\xFE"},
                     std::string {"\xC3"},
                     std::string(300, 'h'),
                     std::string(126, 'h') + Stub::smile})
    {
        auto result = formats.load(description, {.name = name});
        check(result.error.empty());
        check(result.plugin != nullptr);
    }
};

auto tPluginText = test("HostedVST3/malformedTextFromAPluginReadsAsReplacement") = []
{
    // Every broken sequence is one U+FFFD and the rest is kept; a String128 with
    // no terminator is read to its 128th unit and no further.
    auto loaded = Stub::BadTextLoaded();
    const auto layout = loaded.plugin.getBusLayout();

    check(layout.inputs.size() == 1 && layout.outputs.size() == 1);
    check(layout.inputs[0].name == "In" + Stub::replacement);
    check(layout.outputs[0].name == std::string(128, 'o'));
    check(layout.midiInputs.size() == 1);
    check(layout.midiInputs[0].name == Stub::replacement + "M");

    const auto& list = loaded.plugin.parameters();
    check(list.size() == 1);
    check(list[0].name() == "Gain" + Stub::replacement);
    check(list[0].shortName() == std::string(127, 's') + Stub::replacement);
    check(list[0].label() == Stub::smile);
    check(list[0].valueToText(0.5f) == "5" + Stub::replacement + "%");

    // Typed text reaches the plugin terminated, its broken bytes U+FFFD.
    const auto& parsed = loaded.controller->parsed;
    check(near(list[0].textToValue("\xC3"), 0.25f));
    check(near(list[0].textToValue(std::string {"a\xFF"} + "b"), 0.25f));
    const auto fffd = char16_t {0xfffd};
    check(parsed.size() == 2 && parsed[0] == std::u16string {fffd});
    check(parsed.size() == 2 && parsed[1] == std::u16string {u'a', fffd, u'b'});
};

auto tClassText = test("HostedVST3/classesWithBrokenOrWideNamesScanWhole") = []
{
    // The SDK's PluginFactory::classInfos() converts a PClassInfoW a unit at a
    // time, so a pair, let alone a lone surrogate, threw from a noexcept
    // constructor there and ended the process.
    auto stub = Steinberg::owned(new Stub::BadTextFactory());
    const auto factory = ::VST3::Hosting::PluginFactory(
        Steinberg::IPtr<Steinberg::IPluginFactory>(stub.get()));
    const auto classes = audioClassesOf(factory);

    check(classes.size() == 2);

    if (classes.size() != 2)
        return;

    const auto& wide = classes[0];
    check(classIdOf(wide)
          == ::VST3::UID::fromTUID(Stub::BadTextFactory::unicodeId).toString());
    check(wide.category() == kVstAudioEffectClass);
    check(wide.name() == "Smile " + Stub::smile);
    check(wide.vendor() == "V" + Stub::replacement);
    check(wide.version() == std::string(64, '1'));
    check(wide.sdkVersion() == "VST 3.7");
    check(wide.subCategoriesString() == "Fx|Delay");
    check(wide.cardinality() == Steinberg::PClassInfo::kManyInstances);

    // A class with no vendor of its own takes the factory's.
    const auto& plain = classes[1];
    check(classIdOf(plain)
          == ::VST3::UID::fromTUID(Stub::BadTextFactory::plainId).toString());
    check(plain.name() == "Plain");
    check(plain.vendor() == "Stub Vendor");
    check(plain.version() == "1.0");
    check(plain.subCategoriesString() == "Fx");
};
} // namespace
} // namespace HostedVST3Tests
