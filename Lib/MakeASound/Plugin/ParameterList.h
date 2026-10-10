#pragma once

#include "ParameterGroup.h"
#include "../Common/Common.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace MakeASound
{

// FNV-1a of the id, kept to 31 bits because VST3 reserves the ids above. Derived
// from the id alone so inserting a parameter never moves another one's automation.
constexpr uint32_t hostIdFor(std::string_view id) noexcept
{
    auto hash = uint32_t {2166136261u};

    for (auto c: id)
    {
        hash ^= static_cast<uint8_t>(c);
        hash *= 16777619u;
    }

    return hash & 0x7fffffffu;
}

// The flat registry of a parameter group, depth-first in registration order.
// Holds pointers into the group, which must outlive it.
class ParameterList
{
public:
    struct Entry
    {
        Parameter* param = nullptr;
        std::string id;
        std::string displayName;
        uint32_t hostId = 0;
        bool automatable = true;
        bool sessionOnly = false;
    };

    ParameterList() = default;

    explicit ParameterList(ParameterGroup& group);

    int size() const noexcept { return entries.size(); }
    bool empty() const noexcept { return entries.empty(); }

    Parameter& operator[](int index) const noexcept { return *entries[index].param; }
    const Entry& entry(int index) const noexcept { return entries[index]; }

    // -1 when absent.
    int indexOf(const Parameter* param) const noexcept;
    int indexOf(std::string_view id) const noexcept;
    int indexOfHostId(uint32_t hostId) const noexcept;

    // Bounds-tolerant: format adapters pass host indices through unvalidated.
    bool isHostExposed(int index) const noexcept;

    auto begin() const noexcept { return entries.begin(); }
    auto end() const noexcept { return entries.end(); }

private:
    struct HostSlot
    {
        uint32_t hostId = 0;
        int index = 0;
    };

    void add(Parameter& param,
             std::string_view id,
             std::string_view displayName,
             bool automatable);
    void indexHostIds();

    Vector<Entry> entries;
    Vector<HostSlot> byHostId;
};

} // namespace MakeASound
