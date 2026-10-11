#include "Description.h"

namespace MakeASound
{

std::string_view defaultSubcategory(Category category) noexcept
{
    switch (category)
    {
        case Category::Instrument:
            return "Instrument|Synth";
        case Category::MidiEffect:
            return "Instrument";
        case Category::Effect:
            break;
    }

    return "Fx";
}

} // namespace MakeASound
