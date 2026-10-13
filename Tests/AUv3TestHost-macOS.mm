#include "AUv3TestHost.h"

AudioComponentInstantiationOptions AUv3Host::inProcessOption() noexcept
{
    return kAudioComponentInstantiation_LoadInProcess;
}
