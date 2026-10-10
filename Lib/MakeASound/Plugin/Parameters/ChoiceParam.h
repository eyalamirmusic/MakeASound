#pragma once

#include "Parameter.h"
#include "../../Common/Common.h"

#include <atomic>

namespace MakeASound
{

// A list of named choices. The plain value is the index, but saved state stores
// the name, so reordering or extending the list in a later release leaves a
// preset on the choice the user picked.
class ChoiceParam : public Parameter
{
public:
    ChoiceParam(std::string_view nameToUse,
                Vector<std::string> choicesToUse,
                int defaultIndex = 0,
                ParameterOptions options = {});

    float minValue() const noexcept override { return 0.f; }
    float maxValue() const noexcept override;
    float defaultValue() const noexcept override;
    int numSteps() const noexcept override { return numChoices() - 1; }

    void setValue(float plain) noexcept override;
    float getValue() const noexcept override;

    std::string valueToText(float plain) const override;
    float textToValue(std::string_view text) const override;

    StateFormat stateFormat() const noexcept override { return StateFormat::Text; }
    std::string toStateText() const override;
    bool fromStateText(std::string_view text) override;

    int getIndex() const noexcept { return index.load(std::memory_order_relaxed); }
    int numChoices() const noexcept { return choices.size(); }
    const std::string& choiceName(int choice) const noexcept;

    // Exact match first, then ignoring case and surrounding space; -1 if none.
    int indexOfChoice(std::string_view text) const noexcept;

private:
    int clampIndex(int choice) const noexcept;

    Vector<std::string> choices;
    int initial;
    std::atomic<int> index;
};

} // namespace MakeASound
