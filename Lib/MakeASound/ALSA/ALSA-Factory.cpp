#include "ALSAMidiManager.h"

namespace MakeASound
{

// The one TU that names a backend on Linux; Lib/CMakeLists.txt builds this file
// or one of its siblings, never two.
OwningPointer<MidiBackend> makeMidiBackend()
{
    return EA::makeOwned<ALSA::MidiManager>();
}

} // namespace MakeASound
