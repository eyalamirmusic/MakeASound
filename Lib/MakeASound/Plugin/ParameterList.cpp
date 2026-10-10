#include "ParameterList.h"

#include <algorithm>
#include <cassert>
#include <cstdio>

namespace MakeASound
{

ParameterList::ParameterList(ParameterGroup& group)
{
    group.forEach([this](Parameter& param,
                         std::string_view id,
                         std::string_view displayName,
                         bool automatable)
                  { add(param, id, displayName, automatable); });

    indexHostIds();
}

void ParameterList::add(Parameter& param,
                        std::string_view id,
                        std::string_view displayName,
                        bool automatable)
{
    assert(param.explicitHostId().value_or(0) < 0x80000000u
           && "VST3 reserves host ids from 2^31 up");

    entries.add(Entry {&param,
                       std::string {id},
                       std::string {displayName},
                       param.explicitHostId().value_or(hostIdFor(id)),
                       automatable,
                       param.isSessionOnly()});
}

void ParameterList::indexHostIds()
{
    byHostId.clear();
    byHostId.reserve(entries.size());

    for (auto i = 0; i < entries.size(); ++i)
        byHostId.add(HostSlot {entries[i].hostId, i});

    std::sort(byHostId.begin(),
              byHostId.end(),
              [](const HostSlot& a, const HostSlot& b)
              { return a.hostId < b.hostId; });

#ifndef NDEBUG
    for (auto i = 1; i < byHostId.size(); ++i)
    {
        if (byHostId[i].hostId != byHostId[i - 1].hostId)
            continue;

        std::fprintf(stderr,
                     "MakeASound: parameters \"%s\" and \"%s\" share host id %u; "
                     "give one an explicit hostId\n",
                     entries[byHostId[i - 1].index].id.c_str(),
                     entries[byHostId[i].index].id.c_str(),
                     static_cast<unsigned>(byHostId[i].hostId));
        assert(false && "two parameters share a host id");
    }
#endif
}

int ParameterList::indexOf(const Parameter* param) const noexcept
{
    for (auto i = 0; i < entries.size(); ++i)
        if (entries[i].param == param)
            return i;

    return -1;
}

int ParameterList::indexOf(std::string_view id) const noexcept
{
    for (auto i = 0; i < entries.size(); ++i)
        if (entries[i].id == id)
            return i;

    return -1;
}

int ParameterList::indexOfHostId(uint32_t hostId) const noexcept
{
    auto found = std::lower_bound(byHostId.begin(),
                                  byHostId.end(),
                                  hostId,
                                  [](const HostSlot& slot, uint32_t wanted)
                                  { return slot.hostId < wanted; });

    if (found == byHostId.end() || found->hostId != hostId)
        return -1;

    return found->index;
}

bool ParameterList::isHostExposed(int index) const noexcept
{ return index >= 0 && index < size() && entries[index].automatable; }

} // namespace MakeASound
