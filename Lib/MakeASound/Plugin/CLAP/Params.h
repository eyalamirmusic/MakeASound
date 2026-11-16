#pragma once

#include "../Parameters/ParameterList.h"

#include <clap/ext/params.h>

#include <string_view>

namespace MakeASound::CLAP
{

// How one parameter's value reaches a CLAP host, by the identity's
// ClapValueSpace: its plain range, stepped, where its steps are the whole
// numbers of that range under Native; its normalized value otherwise, stepped
// only for a boolean under JuceNormalized.
struct ValueMapping
{
    bool plain = false;
    bool stepped = false;
    bool isEnum = false;
    double minValue = 0.0;
    double maxValue = 1.0;
};

ValueMapping valueMappingFor(const Parameter& param, ClapValueSpace space) noexcept;

// The value a host sees for a normalized one, and back, clamped into the range
// and, where the mapping is plain, onto the parameter's grid.
double toPublished(const Parameter& param,
                   const ValueMapping& mapping,
                   float normalized) noexcept;
float toNormalized(const Parameter& param,
                   const ValueMapping& mapping,
                   double published) noexcept;

// The info a host reads for entry `index` of `list`: its clap_id, its flags
// (automatable, stepped, enum, bypass), its own name, its module (the enclosing
// groups' names, outermost first, joined by '/'), its range and default.
void fillParamInfo(const ParameterList& list,
                   int index,
                   const ValueMapping& mapping,
                   clap_param_info& info) noexcept;

// UTF-8 into a host's fixed buffer of `capacity` bytes, terminated, cut short
// at a code point boundary where it does not fit. Nothing for no room.
void copyText(std::string_view text, char* buffer, int capacity) noexcept;

// clap.params, over the Adapter behind the plugin it is asked of.
const clap_plugin_params* paramsExtension() noexcept;

} // namespace MakeASound::CLAP
