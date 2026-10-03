#include "RTMidi-Backend.h"

#include <CoreMIDI/CoreMIDI.h>

// Defined in RtMidi.cpp but not declared in RtMidi.h.
void RtMidi_setCoreMidiClientSingleton(MIDIClientRef client);

namespace MakeASound::RTMidi
{

bool prepareMidiClient()
{
    // Never disposed, like the singleton RtMidi keeps for itself: a Core MIDI
    // client is per process, and RtMidi objects built later still name it.
    static auto client = MIDIClientRef {};

    if (client != 0)
        return true;

    if (MIDIClientCreate(CFSTR("MakeASound"), nullptr, nullptr, &client) != noErr)
    {
        client = 0;
        return false;
    }

    RtMidi_setCoreMidiClientSingleton(client);
    return true;
}

} // namespace MakeASound::RTMidi
