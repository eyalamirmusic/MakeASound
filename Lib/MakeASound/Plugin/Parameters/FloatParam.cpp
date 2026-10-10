#include "FloatParam.h"

#include <cassert>
#include <cmath>

namespace MakeASound
{

FloatParam::FloatParam(std::string_view nameToUse,
                       float minToUse,
                       float maxToUse,
                       float defaultToUse,
                       ParameterOptions options)
    : Parameter(nameToUse, options)
    , minimum(minToUse)
    , maximum(maxToUse)
    , initial(clamp(defaultToUse))
    , value(initial)
{ assert(minToUse <= maxToUse); }

float FloatParam::clamp(float plain) const noexcept
{ return plain < minimum ? minimum : (plain > maximum ? maximum : plain); }

void FloatParam::setValue(float plain) noexcept
{
    // A NaN would pass any clamp and settle into filter state for the session.
    if (!std::isfinite(plain))
        return;

    value.store(clamp(plain), std::memory_order_relaxed);
}

} // namespace MakeASound
