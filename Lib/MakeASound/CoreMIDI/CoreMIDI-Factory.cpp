#include "CoreMIDIManager.h"

namespace MakeASound
{

// The one TU that names a backend on Apple; Lib/CMakeLists.txt builds this file
// or RTMidi-Factory.cpp, never both.
OwningPointer<MidiBackend> makeMidiBackend()
{
    return EA::makeOwned<CoreMIDI::MidiManager>();
}

} // namespace MakeASound
