#include "ChoiceParam.h"

#include <cmath>

namespace MakeASound
{

ChoiceParam::ChoiceParam(std::string_view nameToUse,
                         Vector<std::string> choicesToUse,
                         int defaultIndex,
                         ParameterOptions options)
    : Parameter(nameToUse, options)
    , choices(std::move(choicesToUse))
    , initial(0)
    , index(0)
{
    if (choices.empty())
        choices.add(std::string {});

    initial = clampIndex(defaultIndex);
    index.store(initial);
}

float ChoiceParam::maxValue() const noexcept
{
    return static_cast<float>(numChoices() - 1);
}

float ChoiceParam::defaultValue() const noexcept
{
    return static_cast<float>(initial);
}

int ChoiceParam::clampIndex(int choice) const noexcept
{
    auto last = numChoices() - 1;
    return choice < 0 ? 0 : (choice > last ? last : choice);
}

void ChoiceParam::setValue(float plain) noexcept
{
    if (!std::isfinite(plain))
        return;

    index.store(indexFor(plain), std::memory_order_relaxed);
}

// Half to even, as JUCE's AudioParameterChoice rounds through roundToInt, so a
// plain write and a normalized one agree at a half point.
int ChoiceParam::indexFor(float plain) const noexcept
{
    auto clamped = plain < 0.f ? 0.f : (plain > maxValue() ? maxValue() : plain);
    return clampIndex(static_cast<int>(roundToIndex(clamped)));
}

float ChoiceParam::getValue() const noexcept
{
    return static_cast<float>(getIndex());
}

const std::string& ChoiceParam::choiceName(int choice) const noexcept
{
    return choices[clampIndex(choice)];
}

std::string ChoiceParam::valueToText(float plain) const
{
    if (!std::isfinite(plain))
        return choiceName(initial);

    return choiceName(indexFor(plain));
}

float ChoiceParam::textToValue(std::string_view text) const
{
    auto found = indexOfChoice(text);
    return static_cast<float>(found >= 0 ? found : initial);
}

int ChoiceParam::indexOfChoice(std::string_view text) const noexcept
{
    for (auto i = 0; i < numChoices(); ++i)
        if (choices[i] == text)
            return i;

    for (auto i = 0; i < numChoices(); ++i)
        if (equalsIgnoringCase(choices[i], trim(text)))
            return i;

    return -1;
}

std::string ChoiceParam::toStateText() const
{
    return choiceName(getIndex());
}

bool ChoiceParam::fromStateText(std::string_view text)
{
    auto found = indexOfChoice(text);

    if (found < 0)
        return false;

    index.store(found, std::memory_order_relaxed);
    return true;
}

} // namespace MakeASound
