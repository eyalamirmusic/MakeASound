#include "RTMidiManager.h"

namespace MakeASound
{

// The one TU that names a backend. A native one takes this file's place per
// platform in Lib/CMakeLists.txt, the way DeviceQueries already does.
OwningPointer<MidiBackend> makeMidiBackend()
{
    return EA::makeOwned<RTMidi::MidiManager>();
}

} // namespace MakeASound
