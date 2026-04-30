#pragma once

#include "../MidiManager/MidiManager.h"
#include "Dropdown.h"

namespace MakeASound
{

class UIMidiManager
{
public:
    explicit UIMidiManager(MidiManager& managerToUse);

    UI::ToggleListInfo makeInputPortToggleList() const;

private:
    MidiManager* manager;
};

} // namespace MakeASound
