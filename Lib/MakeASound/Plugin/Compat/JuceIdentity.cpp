#include "JuceIdentity.h"

namespace MakeASound::Compat
{

namespace
{
uint32_t bigEndianWord(const std::array<uint8_t, 16>& bytes, size_t first) noexcept
{
    return (uint32_t(bytes[first]) << 24) | (uint32_t(bytes[first + 1]) << 16)
           | (uint32_t(bytes[first + 2]) << 8) | uint32_t(bytes[first + 3]);
}

// AAXPluginId's validChars.
constexpr std::string_view aaxLetters =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

// The 35 entries of JUCE's channelSets.
constexpr int aaxChannelSetCount = 35;
} // namespace

Vst3ClassId juceVst3ClassId(FourCC manufacturer, FourCC plugin) noexcept
{
    return {0xABCDEF01u, 0x9182FAEBu, manufacturer.toUint32(), plugin.toUint32()};
}

Vst3ClassId juceVst2ClassId(uint32_t vstUniqueId, std::string_view name) noexcept
{
    auto bytes = std::array<uint8_t, 16> {};
    bytes[0] = 'V';
    bytes[1] = 'S';
    bytes[2] = 'T';
    bytes[3] = static_cast<uint8_t>(vstUniqueId >> 24);
    bytes[4] = static_cast<uint8_t>(vstUniqueId >> 16);
    bytes[5] = static_cast<uint8_t>(vstUniqueId >> 8);
    bytes[6] = static_cast<uint8_t>(vstUniqueId);

    // JUCE stops at the first NUL of its C string.
    for (auto index = size_t {7}; index < bytes.size() && !name.empty(); ++index)
    {
        auto c = static_cast<uint8_t>(name.front());

        if (c == 0)
            break;

        bytes[index] = (c >= 'A' && c <= 'Z') ? static_cast<uint8_t>(c + 32) : c;
        name.remove_prefix(1);
    }

    return {bigEndianWord(bytes, 0),
            bigEndianWord(bytes, 4),
            bigEndianWord(bytes, 8),
            bigEndianWord(bytes, 12)};
}

int juceAAXChannelSetIndex(int channels) noexcept
{
    return channels >= 0 && channels <= 2 ? channels : -1;
}

uint32_t
    juceAAXPluginId(int inputSetIndex, int outputSetIndex, bool audioSuite) noexcept
{
    // Indices into aaxLetters: 'j', 'c' or 'y', 'a', 'a'.
    int letters[] = {9, audioSuite ? 24 : 2, 0, 0};

    auto advance = [&](int position, int setIndex)
    {
        if (setIndex < 0 || setIndex >= aaxChannelSetCount)
            return;

        if (letters[position] + setIndex < static_cast<int>(aaxLetters.size()))
            letters[position] += setIndex;
    };

    advance(2, inputSetIndex);
    advance(3, outputSetIndex);

    auto id = uint32_t {0};

    for (auto letter: letters)
        id = (id << 8)
             | static_cast<uint8_t>(aaxLetters[static_cast<size_t>(letter)]);

    return id;
}

JuceAUParameterInfo juceAUParameterInfo(const Parameter& param,
                                        bool automatable,
                                        IdScheme scheme,
                                        const JuceSettings& settings) noexcept
{
    auto legacyType = settings.forceLegacyParameterAutomationType;
    auto info = JuceAUParameterInfo {};

    info.discrete = param.isDiscrete();
    info.boolean = param.isBoolean();

    // JUCE's getNumSteps() - 1 for a discrete parameter: the number of choices
    // less one, which is MakeASound's numSteps().
    info.maxValue =
        info.discrete && !legacyType ? static_cast<float>(param.numSteps()) : 1.f;
    info.defaultValue = info.toHost(param.toNormalized(param.defaultValue()));

    info.indexedUnit = info.discrete && !legacyType && !info.boolean;
    info.highResolution = !legacyType;
    info.canRamp = !info.discrete;
    info.nonRealTime = param.name().empty() || !automatable;

    info.v2ValueStrings = info.discrete && scheme != IdScheme::JUCELegacyIndex;
    info.v3ValueStrings = info.discrete && !legacyType;
    return info;
}

} // namespace MakeASound::Compat
