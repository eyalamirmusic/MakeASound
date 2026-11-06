#include "AUv3TestHost.h"

// iOS has no LoadInProcess option; a registered subclass is in process anyway.
AudioComponentInstantiationOptions AUv3Host::inProcessOption() noexcept
{
    return 0;
}
