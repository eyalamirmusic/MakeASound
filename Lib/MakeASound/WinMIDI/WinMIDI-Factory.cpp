#include "WinMidiManager.h"

namespace MakeASound
{

// The one TU that names a backend on Windows; Lib/CMakeLists.txt builds this
// file or one of its siblings, never two.
OwningPointer<MidiBackend> makeMidiBackend()
{
    return EA::makeOwned<WinMIDI::MidiManager>();
}

} // namespace MakeASound
