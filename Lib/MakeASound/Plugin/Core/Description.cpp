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

bool vst3MidiControllersFor(const PluginDescription& plugin) noexcept
{
    return plugin.vst3MidiControllers.value_or(plugin.category
                                               == Category::Instrument);
}

} // namespace MakeASound
