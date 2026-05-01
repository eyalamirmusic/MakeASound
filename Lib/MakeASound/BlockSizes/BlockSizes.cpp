#include "BlockSizes.h"

namespace MakeASound
{

Vector<int> getSupportedBlockSizes(int /*deviceId*/)
{
    auto sizes = Vector<int>();

    for (auto size = 64; size <= 2048; size *= 2)
        sizes.add(size);

    return sizes;
}

} // namespace MakeASound
