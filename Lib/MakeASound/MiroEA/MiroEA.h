#pragma once

// Miro <-> ea_data_structures bridge: lets MIRO_REFLECT'd structs hold
// Vector / Array fields and serialize them through Miro's normal
// pipeline by delegating to the underlying std::vector / std::array
// reflection that Miro already provides.

#include "../Common/Common.h"
#include <Miro/Miro.h>

namespace Miro
{

template <typename T>
void reflect(Reflector& ref, MakeASound::Vector<T>& value)
{
    reflect(ref, value.getVector());
}

template <typename T, int N>
void reflect(Reflector& ref, MakeASound::Array<T, N>& value)
{
    reflect(ref, value.getArray());
}

} // namespace Miro
