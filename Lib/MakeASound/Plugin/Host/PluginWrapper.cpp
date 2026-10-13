#include "PluginWrapper.h"

#include "../Realtime/MessageThread.h"
#include "../../Realtime/ScopedNoDenormals.h"

#include <Miro/Miro.h>

#include <algorithm>
#include <cassert>
#include <exception>
#include <future>
#include <utility>

namespace MakeASound
{

namespace
{
// The 10 ms the crossfade takes across a bypass toggle.
constexpr auto fadesPerSecond = 100;

float stepTowards(float from, float target, float step) noexcept
{
    return from < target ? std::min(target, from + step)
                         : std::max(target, from - step);
}
} // namespace

PluginWrapper::PluginWrapper(OwningPointer<Plugin> pluginToUse,
                             PluginFormat format,
                             ParameterIdentity identity)
    : pluginPtr(std::move(pluginToUse))
    , layout(pluginPtr->getBusLayout())
    , holdCounts(addBypassAndCount(*pluginPtr, layout, identity), 0)
    , live(std::make_shared<Live>())
{
    live->plugin = pluginPtr.get();
    pluginPtr->setFormat(format);
    context.prepare(layout);
}

PluginWrapper::~PluginWrapper()
{
    assert(isMessageThread());
}

bool PluginWrapper::addsBypass(const Plugin& plugin)
{
    return plugin.parameters().bypassIndex() < 0
           && !plugin.getBusLayout().inputs.empty();
}

// Decided once, on the declared layout, so the parameter list a host reads at
// construction never changes under it, whatever layout is negotiated later.
int PluginWrapper::addBypassAndCount(Plugin& plugin,
                                     const BusLayout& declared,
                                     ParameterIdentity identity)
{
    if (plugin.parameters().bypassIndex() < 0 && !declared.inputs.empty())
        plugin.addHostBypass();

    // After the bypass, so it takes its scheme's id too.
    plugin.setParameterIdentity(identity);
    return plugin.parameters().size();
}

bool PluginWrapper::setLayout(const BusLayout& proposed)
{
    if (!pluginPtr->acceptsLayout(proposed))
        return false;

    layout = proposed;
    context.prepare(layout);
    return true;
}

void PluginWrapper::prepare(int sampleRate, int maxBlockSize)
{
    pluginPtr->prepare({sampleRate, maxBlockSize, layout});
    prepareBypass(sampleRate, maxBlockSize);
}

void PluginWrapper::prepareBypass(int sampleRate, int maxBlockSize)
{
    auto* bypass = pluginPtr->activeHostBypass();

    if (bypass == nullptr)
        return;

    auto block = std::max(1, maxBlockSize);
    auto mainInputs = layout.inputs.empty() ? 0 : layout.inputs[0].numChannels;
    pluginPtr->bypassDelay.prepare(
        mainInputs, block, std::max(0, pluginPtr->latencySamples()));

    fadeInputs.clear();
    fadeOutputs.clear();

    for (const auto& bus: layout.inputs)
        fadeInputs.add(Buffer(std::max(0, bus.numChannels), block));

    for (const auto& bus: layout.outputs)
        fadeOutputs.add(Buffer(std::max(0, bus.numChannels), block));

    hostInputs.clear();
    hostInputs.resize(layout.inputs.size());
    hostOutputs.clear();
    hostOutputs.resize(layout.outputs.size());

    fadeCapacity = block;
    fadeStep =
        sampleRate > 0
            ? 1.f / static_cast<float>(std::max(1, sampleRate / fadesPerSecond))
            : 1.f;
    bypassMix = bypass->isOn() ? 1.f : 0.f;
}

void PluginWrapper::reset() noexcept
{
    pluginPtr->reset();

    // After a gap there is nothing to fade from, nor anything to delay.
    if (auto* bypass = pluginPtr->activeHostBypass())
    {
        bypassMix = bypass->isOn() ? 1.f : 0.f;
        pluginPtr->bypassDelay.clear();
    }
}

bool PluginWrapper::takeLatencyChanged() noexcept
{
    auto changed = pluginPtr->takeLatencyChanged();
    pluginPtr->fitBypassDelay();
    return changed;
}

std::string PluginWrapper::saveState(StateContext stateContext)
{
    return withBypass(savePluginState(stateContext), stateContext);
}

std::string PluginWrapper::withBypass(std::string document,
                                      StateContext stateContext) const
{
    auto* bypass = pluginPtr->activeHostBypass();

    if (bypass == nullptr || stateContext != StateContext::Session)
        return document;

    // A plugin that saves nothing still has its bypass to keep.
    auto value = document.empty() ? Miro::Json::Value {Miro::Json::Object {}}
                                  : Miro::Json::getParsedValue(document);

    if (!value.isObject())
        return document;

    value.asObject()[bypassStateKey] = Miro::Json::Value {bypass->isOn()};
    return Miro::Json::print(value);
}

std::string_view PluginWrapper::takeBypass(std::string_view document,
                                           StateContext stateContext,
                                           std::string& stripped)
{
    auto* bypass = pluginPtr->activeHostBypass();

    if (bypass == nullptr)
        return document;

    auto value = Miro::Json::getParsedValue(document);

    if (!value.isObject())
        return document;

    auto& object = value.asObject();
    const auto* stored = Miro::Json::find(object, bypassStateKey);

    // Session-only, as a parameter would be: a missing key is the default.
    if (stateContext == StateContext::Session)
        bypass->setValue(
            stored != nullptr && stored->isBool() && stored->asBool() ? 1.f : 0.f);

    if (stored == nullptr)
        return document;

    object.erase(std::string(bypassStateKey));
    stripped = object.empty() ? std::string() : Miro::Json::print(value);
    return stripped;
}

std::string PluginWrapper::savePluginState(StateContext stateContext)
{
    if (isMessageThread())
    {
        assert(pluginPtr->isStateSnapshotCurrent());
        return pluginPtr->saveState(stateContext);
    }

    if (auto snapshot = pluginPtr->saveStateWithoutMessageThread(stateContext);
        !snapshot.empty())
    {
        return snapshot;
    }

    // Waits below, so nothing captured by pointer outlives the call.
    auto document = std::promise<std::string>();
    callOnMessageThread(
        [this, stateContext, promise = &document]
        {
            try
            {
                promise->set_value(pluginPtr->saveState(stateContext));
            }
            catch (...)
            {
                promise->set_exception(std::current_exception());
            }
        });

    return document.get_future().get();
}

void PluginWrapper::loadState(std::string_view document, StateContext stateContext)
{
    auto stripped = std::string();
    auto data = takeBypass(document, stateContext, stripped);

    auto stamp = live->latestLoad.fetch_add(1, std::memory_order_acq_rel) + 1;
    pluginPtr->loadParameters(data, stateContext);

    if (isMessageThread())
    {
        pluginPtr->loadStateExceptParameters(data, stateContext);
        return;
    }

    callOnMessageThread(
        [weak = std::weak_ptr<Live>(live),
         document = std::string(data),
         stateContext,
         stamp]
        {
            auto alive = weak.lock();

            if (alive && alive->latestLoad.load(std::memory_order_acquire) == stamp)
                alive->plugin->loadStateExceptParameters(document, stateContext);
        });
}

bool PluginWrapper::loadForeignState(const ForeignState& state)
{
    // A stamp like loadState's, so a queued load of either kind gives way.
    auto stamp = live->latestLoad.fetch_add(1, std::memory_order_acq_rel) + 1;

    if (isMessageThread())
        return applyForeignState(*pluginPtr, state);

    callOnMessageThread(
        [weak = std::weak_ptr<Live>(live),
         bytes = std::string(state.bytes),
         foreign = state,
         stamp]
        {
            auto alive = weak.lock();

            if (!alive || alive->latestLoad.load(std::memory_order_acquire) != stamp)
                return;

            auto copy = foreign;
            copy.bytes = bytes;

            // The host read its values when the load returned, before this ran.
            if (applyForeignState(*alive->plugin, copy))
                alive->plugin->notifyHostParameterValuesChanged();
        });

    return true;
}

void PluginWrapper::setTrackInfo(TrackInfo info)
{
    if (isMessageThread())
    {
        pluginPtr->setTrackInfo(std::move(info));
        return;
    }

    callOnMessageThread(
        [weak = std::weak_ptr<Live>(live), info = std::move(info)]() mutable
        {
            if (auto alive = weak.lock())
                alive->plugin->setTrackInfo(std::move(info));
        });
}

bool PluginWrapper::applyForeignState(Plugin& plugin, const ForeignState& state)
{
    try
    {
        if (!plugin.loadForeignState(state))
        {
            // A Tamber Plug port's AU session: Plug's document is ours.
            auto plugAsNative =
                state.origin == ForeignOrigin::TamberPlugAU
                && plugin.parameters().identity().foreignPlugStateIsNative;

            if (!plugAsNative)
                return false;

            plugin.loadState(state.bytes, state.context);
        }
    }
    catch (...)
    {
        return false;
    }

    // Session-only, as the added bypass is in a document of ours; one the state
    // did not carry stays as it was, as in JUCE.
    if (state.bypass && state.context == StateContext::Session)
        if (auto* bypass = plugin.activeHostBypass())
            bypass->setValue(*state.bypass ? 1.f : 0.f);

    return true;
}

Parameter* PluginWrapper::parameterAt(int index) const noexcept
{
    auto& list = pluginPtr->parameters();

    if (index < 0 || index >= list.size())
        return nullptr;

    return &list[index];
}

Parameter* PluginWrapper::writableParameterAt(int index) const noexcept
{
    return isParameterHeld(index) ? nullptr : parameterAt(index);
}

void PluginWrapper::setNormalizedParameter(int index, float normalized) noexcept
{
    if (auto* param = writableParameterAt(index))
        param->setNormalized(normalized);
}

void PluginWrapper::setParameter(int index, float plain) noexcept
{
    if (auto* param = writableParameterAt(index))
        param->setValue(plain);
}

float PluginWrapper::getParameter(int index) const noexcept
{
    auto* param = parameterAt(index);
    return param != nullptr ? param->getValue() : 0.f;
}

float PluginWrapper::getNormalizedParameter(int index) const noexcept
{
    auto* param = parameterAt(index);
    return param != nullptr ? param->getNormalized() : 0.f;
}

void PluginWrapper::setParameterByHostId(uint32_t hostId, float normalized) noexcept
{
    setNormalizedParameter(pluginPtr->parameters().indexOfHostId(hostId),
                           normalized);
}

void PluginWrapper::holdParameter(int index) noexcept
{
    if (index >= 0 && index < holdCounts.size())
        holdCounts[index].fetch_add(1, std::memory_order_release);
}

void PluginWrapper::releaseParameter(int index) noexcept
{
    if (index < 0 || index >= holdCounts.size())
        return;

    auto& count = holdCounts[index];
    auto current = count.load(std::memory_order_relaxed);

    while (current > 0
           && !count.compare_exchange_weak(
               current, current - 1, std::memory_order_release))
    {
    }
}

bool PluginWrapper::isParameterHeld(int index) const noexcept
{
    if (index < 0 || index >= holdCounts.size())
        return false;

    return holdCounts[index].load(std::memory_order_acquire) > 0;
}

void PluginWrapper::setPlayhead(const Playhead& playhead) noexcept
{
    context.playhead = playhead;
}

void PluginWrapper::clearMidi() noexcept
{
    context.clearMidi();
}

bool PluginWrapper::pushMidiIn(int bus, const MIDI::Event& event) noexcept
{
    if (bus < 0 || bus >= context.midiIn.size())
        return false;

    auto& buffer = context.midiIn[bus];

    if (buffer.size() >= buffer.capacity())
        return false;

    buffer.add(event);
    return true;
}

void PluginWrapper::sortMidiInByOffset() noexcept
{
    for (auto& buffer: context.midiIn)
        buffer.sortByOffset();
}

void PluginWrapper::bindInput(int bus,
                              const float* const* channels,
                              int numChannels,
                              int numSamples) noexcept
{
    if (bus < 0 || bus >= context.inputs.size())
        return;

    // Where the host's const promise ends: inputs is a mutable vector, and the
    // plugin is trusted not to write through it.
    context.inputs[bus].referTo(
        const_cast<float* const*>(channels), numChannels, numSamples);
}

void PluginWrapper::bindOutput(int bus,
                               float* const* channels,
                               int numChannels,
                               int numSamples,
                               const float* const* matchingInput,
                               int matchingInputChannels) noexcept
{
    if (bus < 0 || bus >= context.outputs.size())
        return;

    context.outputs[bus].referTo(channels, numChannels, numSamples);

    auto inputChannels = matchingInput != nullptr ? matchingInputChannels : 0;

    for (auto ch = 0; ch < numChannels; ++ch)
    {
        auto* out = channels[ch];

        if (ch >= inputChannels)
            std::fill_n(out, numSamples, 0.f);
        else if (matchingInput[ch] != out)
            std::copy_n(matchingInput[ch], numSamples, out);
    }
}

void PluginWrapper::process() noexcept
{
    auto noDenormals = ScopedNoDenormals();

    context.clearMidiOut();

    if (const auto* bypass = pluginPtr->activeHostBypass())
        processBypassable(*bypass);
    else
        pluginPtr->process(context);

    for (auto& buffer: context.inputs)
        buffer.referTo(nullptr, 0, 0);

    for (auto& buffer: context.outputs)
        buffer.referTo(nullptr, 0, 0);
}

void PluginWrapper::processBypassable(const BoolParam& bypass) noexcept
{
    auto target = bypass.isOn() ? 1.f : 0.f;

    if (bypassMix != target && canCrossfade())
    {
        crossfade(target);
        return;
    }

    bypassMix = target;

    if (target > 0.f)
    {
        pluginPtr->processBypassed(context);
        return;
    }

    // Kept current while processing, so a bypass turned on later starts from the
    // input of `latency` samples ago rather than from silence.
    pluginPtr->bypassDelay.push(context.mainInput());
    pluginPtr->process(context);
}

bool PluginWrapper::canCrossfade() const noexcept
{
    if (fadeCapacity <= 0 || fadeInputs.size() != context.inputs.size()
        || fadeOutputs.size() != context.outputs.size())
        return false;

    for (const auto& buffer: context.inputs)
        if (buffer.getNumSamples() > fadeCapacity)
            return false;

    for (const auto& buffer: context.outputs)
        if (buffer.getNumSamples() > fadeCapacity)
            return false;

    return true;
}

// Both sides run: the plugin on the host's buses, processBypassed() on copies.
// The side being faded to runs last, so its MIDI out is the block's.
void PluginWrapper::crossfade(float target) noexcept
{
    for (auto bus = 0; bus < context.inputs.size(); ++bus)
    {
        auto& copy = fadeInputs[bus];
        const auto& host = context.inputs[bus];
        auto channels = std::min(copy.getNumChannels(), host.getNumChannels());

        copy.getSubBuffer(0, host.getNumSamples())
            .getChannelSubset(0, channels)
            .copyFrom(host);
    }

    if (target > 0.f)
    {
        pluginPtr->process(context);
        context.clearMidiOut();
        runBypassedOnCopies();
    }
    else
    {
        runBypassedOnCopies();
        context.clearMidiOut();
        pluginPtr->process(context);
    }

    mixTowards(target);
}

void PluginWrapper::runBypassedOnCopies() noexcept
{
    auto bindingOf = [](const Buffer& buffer)
    {
        return Binding {buffer.getChannelPointers(),
                        buffer.getNumChannels(),
                        buffer.getNumSamples(),
                        buffer.getStartSample()};
    };

    for (auto bus = 0; bus < context.inputs.size(); ++bus)
    {
        auto& buffer = context.inputs[bus];
        auto& copy = fadeInputs[bus];
        hostInputs[bus] = bindingOf(buffer);
        buffer.referTo(copy.getChannelPointers(),
                       std::min(copy.getNumChannels(), buffer.getNumChannels()),
                       buffer.getNumSamples());
    }

    for (auto bus = 0; bus < context.outputs.size(); ++bus)
    {
        auto& buffer = context.outputs[bus];
        auto& scratch = fadeOutputs[bus];
        hostOutputs[bus] = bindingOf(buffer);
        buffer.referTo(scratch.getChannelPointers(),
                       std::min(scratch.getNumChannels(), buffer.getNumChannels()),
                       buffer.getNumSamples());

        // What bindOutput() hands the plugin: the input of the same index, the
        // rest silent.
        buffer.clear();

        if (bus < context.inputs.size())
            buffer.copyFrom(context.inputs[bus]);
    }

    pluginPtr->processBypassed(context);

    for (auto bus = 0; bus < context.inputs.size(); ++bus)
    {
        const auto& host = hostInputs[bus];
        context.inputs[bus].referTo(
            host.channels, host.numChannels, host.numSamples, host.startSample);
    }

    for (auto bus = 0; bus < context.outputs.size(); ++bus)
    {
        const auto& host = hostOutputs[bus];
        context.outputs[bus].referTo(
            host.channels, host.numChannels, host.numSamples, host.startSample);
    }
}

// The host's outputs hold the processed side, the scratch the bypassed one.
void PluginWrapper::mixTowards(float target) noexcept
{
    auto start = bypassMix;
    auto longest = 0;

    for (auto bus = 0; bus < context.outputs.size(); ++bus)
    {
        auto& output = context.outputs[bus];
        const auto& bypassed = fadeOutputs[bus];
        auto shared = std::min(output.getNumChannels(), bypassed.getNumChannels());
        auto length = output.getNumSamples();
        longest = std::max(longest, length);

        for (auto ch = 0; ch < output.getNumChannels(); ++ch)
        {
            auto* processed = output.getChannelPointer(ch);
            const auto* dry = ch < shared ? bypassed.getChannelPointer(ch) : nullptr;
            auto mix = start;

            for (auto i = 0; i < length; ++i)
            {
                mix = stepTowards(mix, target, fadeStep);
                auto other = dry != nullptr ? dry[i] : 0.f;
                processed[i] += (other - processed[i]) * mix;
            }
        }
    }

    // Stepped as the samples were, so it lands on the target exactly. With no
    // output to fade there is nothing to wait for.
    if (context.outputs.empty())
        bypassMix = target;

    for (auto i = 0; i < longest; ++i)
        bypassMix = stepTowards(bypassMix, target, fadeStep);
}

} // namespace MakeASound
