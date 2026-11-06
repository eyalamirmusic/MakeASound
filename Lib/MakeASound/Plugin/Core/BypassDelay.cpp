#include "BypassDelay.h"

#include <algorithm>
#include <utility>

namespace MakeASound
{

void BypassDelay::prepare(int numChannelsToUse, int maxBlockSize, int delaySamples)
{
    auto channels = std::max(0, numChannelsToUse);
    auto block = std::max(1, maxBlockSize);
    auto size = std::max(0, delaySamples) + block;

    auto next = Vector<float>();
    next.resize(channels * size, 0.f);

    {
        auto guard = ScopedSpinLock(lock);
        std::swap(ring, next);
        numChannels = channels;
        ringSize = size;
        blockSize = block;
        writePosition = 0;
    }

    // `next` holds the old ring now, and frees it outside the lock.
}

void BypassDelay::ensureDelay(int delaySamples)
{
    if (ringSize == 0 || std::max(0, delaySamples) + blockSize <= ringSize)
        return;

    prepare(numChannels, blockSize, delaySamples);
}

int BypassDelay::capacity() const noexcept
{
    return ringSize > 0 ? ringSize - 1 : 0;
}

void BypassDelay::clear() noexcept
{
    auto guard = ScopedTryLock(lock);

    if (!guard.held)
        return;

    std::fill(ring.begin(), ring.end(), 0.f);
    writePosition = 0;
}

void BypassDelay::push(const Buffer& input) noexcept
{
    auto guard = ScopedTryLock(lock);

    if (!guard.held || ringSize == 0)
        return;

    auto total = input.getNumSamples();

    for (auto start = 0; start < total; start += ringSize)
    {
        auto length = std::min(ringSize, total - start);
        write(input, start, length);
        writePosition = (writePosition + length) % ringSize;
    }
}

void BypassDelay::process(const Buffer& input, Buffer& output, int delay) noexcept
{
    auto guard = ScopedTryLock(lock);

    if (!guard.held)
    {
        output.clear();
        return;
    }

    if (ringSize == 0)
    {
        copyAcross(input, output);
        return;
    }

    auto clamped = std::clamp(delay, 0, ringSize - 1);
    auto total = output.getNumSamples();

    // Each chunk is written before it is read, so a chunk may span no more than
    // the ring minus the delay, or it would overwrite what it is about to read.
    auto chunk = ringSize - clamped;

    for (auto start = 0; start < total; start += chunk)
    {
        auto length = std::min(chunk, total - start);
        write(input, start, length);
        read(output, start, length, clamped, input.getNumChannels());
        writePosition = (writePosition + length) % ringSize;
    }
}

void BypassDelay::write(const Buffer& input, int start, int length) noexcept
{
    for (auto ch = 0; ch < numChannels; ++ch)
    {
        auto* target = channel(ch);
        auto hasSource = ch < input.getNumChannels();

        for (auto i = 0; i < length; ++i)
        {
            auto position = start + i;
            auto sample = hasSource && position < input.getNumSamples()
                              ? input.getChannelPointer(ch)[position]
                              : 0.f;
            target[(writePosition + i) % ringSize] = sample;
        }
    }
}

void BypassDelay::read(
    Buffer& output, int start, int length, int delay, int inputChannels) noexcept
{
    auto available = std::min(numChannels, inputChannels);
    auto readPosition = (writePosition - delay + ringSize) % ringSize;

    for (auto ch = 0; ch < output.getNumChannels(); ++ch)
    {
        auto source = inputChannels == 1 ? 0 : ch;
        auto* target = output.getChannelPointer(ch) + start;

        if (source >= available)
        {
            std::fill_n(target, length, 0.f);
            continue;
        }

        const auto* from = channel(source);

        for (auto i = 0; i < length; ++i)
            target[i] = from[(readPosition + i) % ringSize];
    }
}

void BypassDelay::copyAcross(const Buffer& input, Buffer& output) noexcept
{
    auto inputChannels = input.getNumChannels();
    auto length = std::min(input.getNumSamples(), output.getNumSamples());

    for (auto ch = 0; ch < output.getNumChannels(); ++ch)
    {
        auto source = inputChannels == 1 ? 0 : ch;
        auto* target = output.getChannelPointer(ch);

        if (source >= inputChannels)
        {
            std::fill_n(target, output.getNumSamples(), 0.f);
            continue;
        }

        const auto* from = input.getChannelPointer(source);

        if (from != target)
            std::copy_n(from, length, target);

        std::fill(target + length, target + output.getNumSamples(), 0.f);
    }
}

} // namespace MakeASound
