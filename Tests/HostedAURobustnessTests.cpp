// Hosting an Audio Unit that misbehaves, or that behaves within the contract in
// ways a careless host trips over (MisbehavingAU.h): a unit that lists its
// parameters, and answers for their info, only once initialized (Soundtoys'
// Decapitator crashes on an info read while uninitialized); one that tells its
// listeners from inside its own Initialize and Render, and cannot be read back
// from there; one that initializes without the host's callbacks; one that turns
// a stream format down; one that pulls more input than the block, or pulls into
// the host's memory and writes there; and parameter info with a cached name it
// never asked the host to release, a name with no terminator, and an id listed
// twice.

#include "MisbehavingAU.h"

#include <MakeASound/Hosting/MakeASoundHosting.h>
#include <MakeASound/Hosting/AU/AUFormat.h>
#include <MakeASound/Hosting/AU/HostedAudioUnit.h>

#include <NanoTest/NanoTest.h>

#include <eacp/Core/Threads/EventLoop.h>

#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace HostedAURobustnessTests
{
using namespace nano;
using namespace MakeASound;
using namespace MakeASound::Hosting;

namespace
{
namespace Misbehaving = ::MisbehavingAU;

constexpr auto blockSize = 64;

void turnTheRunLoop(eacp::Time::MS duration = eacp::Time::MS {250})
{
    eacp::Threads::runEventLoopUntil([] { return false; }, duration);
}

struct LatencyRecorder : HostEditListener
{
    void beginParameterEdit(int) noexcept override {}
    void performParameterEdit(int, float) noexcept override {}
    void endParameterEdit(int) noexcept override {}
    void latencyChanged() noexcept override { ++latencies; }

    int latencies = 0;
};

// A block over memory the test owns, for what lies around it to be checked.
struct OwnedBlock
{
    explicit OwnedBlock(const BusLayout& layout, int numSamples)
    {
        context.prepare(layout);

        for (auto& channel: outputMemory)
            channel.fill(0.f);

        for (auto ch = 0; ch < 2; ++ch)
            outputTable[static_cast<size_t>(ch)] = outputMemory[ch].data();

        context.outputs[0].referTo(
            outputTable.data(), layout.outputs[0].numChannels, numSamples);
    }

    // Input of `value` for the block, and `beyond` past it in the same memory.
    void feedInput(int numSamples, float value, float beyond)
    {
        for (auto ch = 0; ch < 2; ++ch)
        {
            auto& channel = inputMemory[static_cast<size_t>(ch)];
            std::fill(channel.begin(), channel.end(), beyond);
            std::fill_n(channel.begin(), numSamples, value);
            inputTable[static_cast<size_t>(ch)] = channel.data();
        }

        context.inputs[0].referTo(inputTable.data(), 2, numSamples);
    }

    ProcessContext context;
    std::array<std::array<float, 4 * blockSize>, 2> inputMemory {};
    std::array<std::array<float, 4 * blockSize>, 2> outputMemory {};
    std::array<float*, 2> inputTable {};
    std::array<float*, 2> outputTable {};
};

void prepare(HostedAudioUnit& plugin, const BusLayout& layout)
{
    plugin.prepare(
        {.sampleRate = 48000.0, .maxBlockSize = blockSize, .layout = layout});
}

// --- 1. Info read after uninitialize ------------------------------------------

auto tInfoUninitialized =
    test("HostedAURobustness/aUnitListingOnceInitializedIsAskedForInfoOnlyThen") = []
{
    auto behaviour = Misbehaving::Behaviour();
    behaviour.listOnlyOnceInitialized = true;
    auto loaded = Misbehaving::load(behaviour);

    if (!loaded)
        return;

    auto& plugin = *loaded.plugin;
    const auto& record = loaded.unit->record;

    check(record.uninitializedInfoReads == 0,
          "ParameterInfo or ParameterValueStrings was read from a unit that "
          "answers only once initialized, while it was uninitialized");
    check(plugin.parameters().size() == Misbehaving::numParameters,
          "every parameter the initialized unit lists is loaded");

    if (plugin.parameters().size() != Misbehaving::numParameters)
        return;

    check(plugin.parameters()[0].name() == "Gain", "parameter 0 has its name");
    check(plugin.parameters()[1].name() == "Mix", "parameter 1 has its name");
    check(plugin.parameters()[2].name() == "Mode", "parameter 2 has its name");
    check(plugin.parameters()[2].valueToText(1.f) == "On",
          "the indexed parameter has its value strings");
};

// --- 2. refreshInfo() on an uninitialized unit -------------------------------

auto tRefreshUninitialized = test(
    "HostedAURobustness/aRefreshBeforePrepareAsksNoUninitializedUnitForInfo") = []
{
    // Answers anyway, so the load has parameters to refresh.
    auto behaviour = Misbehaving::Behaviour();
    behaviour.listOnlyOnceInitialized = true;
    behaviour.answerInfoWhileUninitialized = true;
    auto loaded = Misbehaving::load(behaviour);

    if (!loaded)
        return;

    auto& plugin = *loaded.plugin;
    auto& unit = *loaded.unit;
    check(plugin.parameters().size() == Misbehaving::numParameters,
          "the unit loads with its parameters");
    check(!plugin.isInitialized(), "the unit is handed over uninitialized");

    // Only what happens after the load counts here.
    unit.record.uninitializedInfoReads = 0;

    Misbehaving::notify(unit, kAudioUnitProperty_ParameterInfo, 0);
    Misbehaving::notify(unit, kAudioUnitProperty_ParameterList);
    turnTheRunLoop();

    check(unit.record.uninitializedInfoReads == 0,
          "refreshing the names read ParameterInfo from an uninitialized unit "
          "that answers only once initialized");
    check(!plugin.isInitialized() && !unit.initialized,
          "the refresh leaves the unit uninitialized, as it found it");
};

// --- 3. Re-entrant property listener -----------------------------------------

auto tReentrantLatency =
    test("HostedAURobustness/aLatencyToldFromInsideTheUnitIsReadAfterItReturns") = []
{
    auto behaviour = Misbehaving::Behaviour();
    behaviour.latencyOnInitialize = 0.01;
    behaviour.latencyOnFirstRender = 0.02;
    auto loaded = Misbehaving::load(behaviour);

    if (!loaded)
        return;

    auto& plugin = *loaded.plugin;
    const auto& record = loaded.unit->record;
    auto listener = LatencyRecorder();
    plugin.setHostEditListener(&listener);

    // Told from inside Initialize...
    auto layout = plugin.getBusLayout();
    auto block = OwnedBlock(layout, blockSize);
    block.feedInput(blockSize, 0.f, 0.f);
    prepare(plugin, layout);
    turnTheRunLoop();

    // ...and from inside Render.
    plugin.process(block.context);
    eacp::Threads::runEventLoopUntil([&] { return plugin.latencySamples() == 960; },
                                     eacp::Time::MS {1000});

    check(record.reentrantReads == 0,
          "the host read a property back from inside the unit's own "
          "property-changed notification");
    check(record.renders == 1, "the block rendered");
    check(plugin.latencySamples() == 960,
          "the latency told from inside Render reaches the host once the run "
          "loop turns");
    check(listener.latencies >= 1, "the host's listener hears of the latency");

    plugin.setHostEditListener(nullptr);
};

// --- 4. Initialize before host callbacks are set -----------------------------

auto tHostCallbacks =
    test("HostedAURobustness/everyInitializeFindsTheHostCallbacksInstalled") = []
{
    // Lists nothing until initialized, so loading initializes it once.
    auto behaviour = Misbehaving::Behaviour();
    behaviour.listOnlyOnceInitialized = true;
    behaviour.answerInfoWhileUninitialized = true;
    auto loaded = Misbehaving::load(behaviour);

    if (!loaded)
        return;

    auto& plugin = *loaded.plugin;
    const auto& record = loaded.unit->record;
    prepare(plugin, plugin.getBusLayout());

    check(record.initializeCalls >= 2, "the load and prepare() both initialized");
    check(record.initializesWithoutHostCallbacks == 0,
          "the unit was initialized before kAudioUnitProperty_HostCallbacks "
          "was set");
};

// --- 5. Ignored stream-format failures ---------------------------------------

auto tRejectedFormat =
    test("HostedAURobustness/aStreamFormatTheUnitTurnedDownNeverReachesRender") = []
{
    // Says it takes any count, but its output takes only two channels.
    auto behaviour = Misbehaving::Behaviour();
    behaviour.anyChannelCount = true;
    behaviour.onlyOutputChannels = 2;
    auto loaded = Misbehaving::load(behaviour);

    if (!loaded)
        return;

    auto& plugin = *loaded.plugin;
    const auto& record = loaded.unit->record;

    auto layout = plugin.getBusLayout();
    layout.outputs[0].numChannels = 1;
    check(plugin.acceptsLayout(layout), "the unit says it takes a mono output");

    auto block = OwnedBlock(layout, blockSize);
    block.feedInput(blockSize, 0.f, 0.f);
    prepare(plugin, layout);
    plugin.process(block.context);

    check(record.inconsistentRenders == 0,
          "Render was handed an output list that does not match the stream "
          "format the unit accepted");

    // As every other failure to prepare reads: the unit left uninitialized,
    // and process() a no-op.
    check(!plugin.isInitialized(),
          "prepare() left the unit initialized though it turned the output "
          "format down");
};

// --- 6. supplyInput over-read and the shared silence -------------------------

auto tOverPull =
    test("HostedAURobustness/aPullPastTheBlockReadsNothingBeyondIt") = []
{
    auto behaviour = Misbehaving::Behaviour();
    behaviour.pullMaxFrames = true;
    auto loaded = Misbehaving::load(behaviour);

    if (!loaded)
        return;

    auto& plugin = *loaded.plugin;
    const auto& record = loaded.unit->record;

    // Half a block, with something else in the memory just past it.
    constexpr auto half = blockSize / 2;
    auto layout = plugin.getBusLayout();
    auto block = OwnedBlock(layout, half);
    block.feedInput(half, 1.f, 7.f);
    prepare(plugin, layout);
    plugin.process(block.context);

    check(record.pulls == 1, "the unit pulled its input");
    check(record.samplesFromBeyondTheBlock == 0,
          "the host supplied input samples from past the end of the block");
};

auto tSilence = test("HostedAURobustness/theSilenceAUnitPullsStaysSilent") = []
{
    auto behaviour = Misbehaving::Behaviour();
    behaviour.pullIntoHostMemory = true;
    auto loaded = Misbehaving::load(behaviour);

    if (!loaded)
        return;

    auto& plugin = *loaded.plugin;
    const auto& record = loaded.unit->record;

    // No input reaches the host: the unit pulls, gets silence, writes there.
    auto layout = plugin.getBusLayout();
    auto block = OwnedBlock(layout, blockSize);
    prepare(plugin, layout);

    plugin.process(block.context);
    plugin.process(block.context);

    check(record.pulls == 2, "the unit pulled its input twice");
    check(record.nonSilentSamples == 0,
          "the silence handed to a unit with no input of its own was not "
          "silent, after the unit wrote into it the block before");
};

// --- 7. CF ownership in takeStrings ------------------------------------------

auto tCachedName =
    test("HostedAURobustness/aNameWithoutHasCFNameStringIsNeverReleased") = []
{
    auto behaviour = Misbehaving::Behaviour();
    behaviour.cachedNameWithoutFlag = true;
    auto loaded = Misbehaving::load(behaviour);

    if (!loaded)
        return;

    auto& plugin = *loaded.plugin;
    auto& unit = *loaded.unit;
    auto* name = unit.cachedName;
    auto before = unit.cachedNameRetains;

    check(CFGetRetainCount(name) == before,
          "loading released a cfNameString the info's flags say is not valid");
    check(plugin.parameters()[0].name() == "Gain", "the name is the C string");

    // A refresh reads the info again.
    Misbehaving::notify(unit, kAudioUnitProperty_ParameterInfo, 0);
    turnTheRunLoop();

    check(CFGetRetainCount(name) == before,
          "refreshing released a cfNameString the info's flags say is not valid");
};

// --- 8. Duplicate parameter ids ----------------------------------------------

// A duplicate reaching the parameter group asserts in a debug build
// (ParameterGroup::addParameter) and aborts the process, so this stays a case
// of its own.
auto tDuplicateId = test("HostedAURobustness/anIdListedTwiceIsOneParameter") = []
{
    auto behaviour = Misbehaving::Behaviour();
    behaviour.ids = {0, 1, 1, 2};
    auto loaded = Misbehaving::load(behaviour);

    if (!loaded)
        return;

    const auto& list = loaded.plugin->parameters();
    check(list.size() == Misbehaving::numParameters,
          "a parameter id the unit listed twice is loaded once");

    auto ids = std::vector<std::string>();

    for (auto i = 0; i < list.size(); ++i)
        ids.push_back(list.entry(i).id);

    std::sort(ids.begin(), ids.end());
    check(std::adjacent_find(ids.begin(), ids.end()) == ids.end(),
          "no two parameters share an id");
};

// --- 9. Unbounded info.name read ---------------------------------------------

auto tUnterminatedName =
    test("HostedAURobustness/anUnterminatedNameStopsAtItsField") = []
{
    auto behaviour = Misbehaving::Behaviour();
    behaviour.unterminatedName = true;
    auto loaded = Misbehaving::load(behaviour);

    if (!loaded)
        return;

    const auto& list = loaded.plugin->parameters();
    check(list.size() == Misbehaving::numParameters, "the unit loads");

    if (list.size() != Misbehaving::numParameters)
        return;

    auto name = list[1].name();
    check(name.size() <= sizeof(AudioUnitParameterInfo::name),
          "a name with no terminator was read past its 52 bytes");
    check(name.find_first_not_of('x') == std::string::npos,
          "the name holds only the name field's bytes");
};
} // namespace
} // namespace HostedAURobustnessTests
