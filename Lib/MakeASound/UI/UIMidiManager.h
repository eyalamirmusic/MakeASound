#pragma once

/** @file UIMidiManager: the MIDI input port toggle list built from a MidiManager. */

#include "../MIDI/MidiManager.h"
#include "Dropdown.h"

namespace MakeASound
{

/// Keeps a pointer to the MidiManager, which must outlive it.
class UIMidiManager
{
public:
    explicit UIMidiManager(MidiManager& managerToUse);

    UI::ToggleListInfo makeInputPortToggleList() const;

private:
    MidiManager* manager;
};

} // namespace MakeASound
