#include "UIMidiManager.h"

namespace MakeASound
{

UIMidiManager::UIMidiManager(MidiManager& managerToUse)
    : manager(&managerToUse)
{
}

UI::ToggleListInfo UIMidiManager::makeInputPortToggleList() const
{
    return UI::makeMidiPortToggleList(manager->getInputPorts(),
                                      manager->getOpenInputPorts());
}

} // namespace MakeASound
