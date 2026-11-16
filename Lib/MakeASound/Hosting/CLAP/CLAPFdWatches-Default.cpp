#include "CLAPFdWatches.h"

namespace MakeASound::Hosting
{

// Only Linux plugins ask a host for its run loop's descriptors.
OwningPointer<CLAPFdWatches> CLAPFdWatches::create(Callback)
{
    return {};
}

} // namespace MakeASound::Hosting
