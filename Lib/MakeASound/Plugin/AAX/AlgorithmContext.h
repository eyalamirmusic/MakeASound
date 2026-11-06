#pragma once

#include "AAXCommon.h"

namespace MakeASound::AAX
{

class Adapter;

// The private-data block the host allocates per instance; ResetFieldData fills it.
struct PrivateData
{
    Adapter* adapter;
};

// What the host hands the process procedure per instance, one field per port the
// component describes. Field order is the ABI: append, never reorder. midiIn and
// midiOut are nodes only when the layout has that MIDI bus, a placeholder port
// otherwise.
#include AAX_ALIGN_FILE_BEGIN
#include AAX_ALIGN_FILE_ALG
#include AAX_ALIGN_FILE_END
struct AlgorithmContext
{
    float** inputs;
    float** outputs;
    int32_t* numSamples;
    AAX_CTimestamp* clock;
    AAX_IMIDINode* midiIn;
    AAX_IMIDINode* midiOut;
    AAX_IMIDINode* transport;
    PrivateData* privateData;
};
#include AAX_ALIGN_FILE_BEGIN
#include AAX_ALIGN_FILE_RESET
#include AAX_ALIGN_FILE_END

// Walks the instances and renders each through its adapter.
void AAX_CALLBACK processProc(AlgorithmContext* const instancesBegin[],
                              const void* instancesEnd);

} // namespace MakeASound::AAX
