#include "ALSAMidiManager.h"

namespace MakeASound
{

// The one TU that names a backend on Linux; Lib/CMakeLists.txt builds this file
// or CoreMIDI-Factory.cpp or RTMidi-Factory.cpp, never two of them.
OwningPointer<MidiBackend> makeMidiBackend()
{
    return EA::makeOwned<ALSA::MidiManager>();
}

} // namespace MakeASound
