#include "View.h"

namespace MakeASound::AAX
{

bool hasAAXView() noexcept
{
    return false;
}

AAX_IEffectGUI* AAX_CALLBACK createAAXView()
{
    return nullptr;
}

void releaseViewDevices()
{
}

} // namespace MakeASound::AAX
