#include "NullMidiManager.h"

namespace MakeASound
{

// The one TU that names a backend where there is no MIDI; Lib/Devices.cmake builds
// this file or one of its siblings, never two.
OwningPointer<MidiBackend> makeMidiBackend()
{
    return EA::makeOwned<NullMIDI::MidiManager>();
}

} // namespace MakeASound
