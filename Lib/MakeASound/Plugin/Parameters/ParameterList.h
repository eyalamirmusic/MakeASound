#pragma once

#include "ParameterGroup.h"
#include "ParameterIdentity.h"
#include "../../Common/Common.h"

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
//
// Each entry carries every id a host addresses it by, computed by the rules of
// the list's ParameterIdentity: MakeASound's by default, a port's profile's
// otherwise (see IdScheme and policyFor()). An adapter reads them from here and
// computes none itself.
class ParameterList
{
public:
    // A named group around an entry.
    struct EnclosingGroup
    {
        // Its id path ("Osc 1/Filter"): the groups' key in saved state.
        std::string id;
        std::string name;

        // The AUv3 AUParameterGroup identifier, by GroupIdRule: the id path,
        // or the group's own id, as JUCE's tree names it.
        std::string identifier;

        // The group's own id, the last segment of the path.
        std::string ownId;
    };

    struct Entry
    {
        Parameter* param = nullptr;
        std::string id;
        std::string displayName;

        // The VST3 ParamID and the AU AudioUnitParameterID, by ParamIdRule:
        // FNV-1a of the id path; String::hashCode of the hostKey, masked to 31
        // bits unless juceMask31 is off; or the index. The added bypass's by
        // AddedBypassId. ParameterOptions::hostId overrides it in every scheme.
        uint32_t hostId = 0;
        bool automatable = true;
        bool sessionOnly = false;

        // True on the one entry bypassIndex() names.
        bool bypass = false;

        // Outermost first; empty for a parameter of the root group.
        Vector<EnclosingGroup> groups;

        // The parameter's hostKey(): the leaf key a JUCE scheme hashes.
        std::string hostKey;

        // The AUv3 AUParameterAddress, by AUv3Ids: the host id;
        // String::hashCode64 of the hostKey, never masked; or the index.
        uint64_t address = 0;

        // The AUv3 AUParameter identifier, by AUv3Ids: the id path or the
        // hostKey.
        std::string identifier;

        // The AAX_CParamID, by AAXIdRule: the host id in eight lowercase hex
        // digits; ParameterOptions::aaxId, else the hostKey; or the index in
        // decimal. Under either of the last two, "MasterBypassID" for the
        // bypass entry, declared or added.
        std::string aaxId;

        // The bypass PluginWrapper added, rather than one the plugin declared.
        bool addedBypass = false;

        // False only for the added bypass under BypassPlacement::Unlisted.
        bool listedInAU = true;

        // Under ParamIdRule::Index, an entry added after the port: its
        // versionHint is past ParameterIdentity::indexedThroughVersion. It takes
        // FNV-1a of its path for every id an index would give, counts toward no
        // other entry's index, and lists in an AU after every indexed entry.
        bool postPort = false;
    };

    ParameterList() = default;

    explicit ParameterList(ParameterGroup& group, ParameterIdentity identity = {});

    // The bypass PluginWrapper adds for an effect that declares none: one more
    // exposed entry, last, so no other index moves, under no group and with its
    // own id's host id. A no-op when the list already has a bypass.
    void addHostBypass(Parameter& param);

    // Recomputes every entry's ids under `identity`; a debug build prints and
    // asserts on identityProblems(). PluginWrapper calls it at construction,
    // so every hosted plugin is checked; a list built on its own is not.
    void setIdentity(ParameterIdentity identityToUse);
    const ParameterIdentity& identity() const noexcept { return ids; }

    // Every clash between the entries' ids, one line each, empty when none: no
    // two host ids, addresses, JUCE host keys, group ids or truncated AAX ids
    // alike, an explicit host id below 2^31 unless JUCE's mask is off, an AAX
    // id at most 31 characters, and no host id among the VST3 MIDI controller
    // parameters' where there are any (a JUCE port's only reported, never
    // asserted). In any build, so a Release test can run it.
    Vector<std::string> identityProblems() const;

    int size() const noexcept { return entries.size(); }
    bool empty() const noexcept { return entries.empty(); }

    Parameter& operator[](int index) const noexcept { return *entries[index].param; }
    const Entry& entry(int index) const noexcept { return entries[index]; }

    // -1 when absent.
    int indexOf(const Parameter* param) const noexcept;
    int indexOf(std::string_view id) const noexcept;
    int indexOfHostId(uint32_t hostId) const noexcept;

    // By Entry::address, through a sorted table: allocation-free.
    int indexOfAddress(uint64_t address) const noexcept;

    // By Entry::aaxId, a linear search: message thread.
    int indexOfAAXId(std::string_view aaxId) const noexcept;

    // Bounds-tolerant: format adapters pass host indices through unvalidated.
    bool isHostExposed(int index) const noexcept;

    // Whether the VST3 and AAX adapters register the entry with the host, by
    // Registration: only a host-exposed one, or every one (a non-automatable
    // one as not automatable, as JUCE's wrappers do). Bounds-tolerant.
    bool isRegistered(int index) const noexcept;

    // The first host-exposed entry whose parameter isBypass(), -1 when none.
    int bypassIndex() const noexcept { return bypass; }

    auto begin() const noexcept { return entries.begin(); }
    auto end() const noexcept { return entries.end(); }

private:
    struct HostSlot
    {
        uint32_t hostId = 0;
        int index = 0;
    };

    struct AddressSlot
    {
        uint64_t address = 0;
        int index = 0;
    };

    void add(Parameter& param,
             std::string_view id,
             std::string_view displayName,
             bool automatable,
             std::span<const ParameterGroup* const> groups);
    void assignIds();
    void finishIds(Entry& entry) const;
    uint32_t derivedHostId(const Entry& entry, int index) const noexcept;
    void indexHostIds();
    void findBypass();
    void findDuplicateHostIds(Vector<std::string>& problems) const;
    Vector<std::string> problemsWith(bool controllerClashes) const;

    // Among the VST3 MIDI controller parameters' ids, where there are any.
    bool isMidiControllerId(uint32_t hostId) const noexcept;

    Vector<Entry> entries;
    Vector<HostSlot> byHostId;
    Vector<AddressSlot> byAddress;
    ParameterIdentity ids;
    int bypass = -1;
};

} // namespace MakeASound
