#include "Channel.h"

namespace MakeASound
{

Channel::Channel(std::span<float> samplesToUse) noexcept
    : samples(samplesToUse)
{
}

Channel::Channel(float* dataToUse, int numSamplesToUse) noexcept
    : samples(dataToUse, static_cast<size_t>(numSamplesToUse))
{
}

int Channel::getNumSamples() const noexcept
{
    return static_cast<int>(samples.size());
}

bool Channel::isEmpty() const noexcept
{
    return samples.empty();
}

float* Channel::data() const noexcept
{
    return samples.data();
}

float& Channel::operator[](std::size_t sample) const noexcept
{
    return samples[sample];
}

float* Channel::begin() const noexcept
{
    return samples.data();
}

float* Channel::end() const noexcept
{
    return samples.data() + samples.size();
}

} // namespace MakeASound
