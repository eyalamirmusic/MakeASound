#include "PluginWrapper.h"

#include "../Realtime/MessageThread.h"
#include "../../Realtime/ScopedNoDenormals.h"

#include <algorithm>
#include <cassert>
#include <exception>
#include <future>
#include <utility>

namespace MakeASound
{

PluginWrapper::PluginWrapper(OwningPointer<Plugin> pluginToUse, PluginFormat format)
    : pluginPtr(std::move(pluginToUse))
    , layout(pluginPtr->getBusLayout())
    , holdCounts(pluginPtr->parameters().size(), 0)
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
}

void PluginWrapper::reset() noexcept
{
    pluginPtr->reset();
}

std::string PluginWrapper::saveState(StateContext stateContext)
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

void PluginWrapper::loadState(std::string_view data, StateContext stateContext)
{
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
    pluginPtr->process(context);

    for (auto& buffer: context.inputs)
        buffer.referTo(nullptr, 0, 0);

    for (auto& buffer: context.outputs)
        buffer.referTo(nullptr, 0, 0);
}

} // namespace MakeASound
