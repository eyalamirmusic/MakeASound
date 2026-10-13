#include "ParameterList.h"
#include "../Compat/JuceState.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <set>

namespace MakeASound
{

namespace
{
std::string hexId(uint32_t hostId)
{
    constexpr char digits[] = "0123456789abcdef";
    auto text = std::string(8, '0');

    for (auto digit = 0; digit < 8; ++digit)
        text[static_cast<size_t>(digit)] =
            digits[(hostId >> (28 - 4 * digit)) & 0xf];

    return text;
}

// Characters as JUCE counts them: one per UTF-8 sequence, a lead byte each.
int codePoints(std::string_view utf8) noexcept
{
    return static_cast<int>(std::count_if(
        utf8.begin(),
        utf8.end(),
        [](char c) { return (static_cast<uint8_t>(c) & 0xc0) != 0x80; }));
}

// The first `count` characters, as JUCE's String::substring cuts them.
std::string_view firstCodePoints(std::string_view utf8, int count) noexcept
{
    auto seen = 0;

    for (auto i = size_t {0}; i < utf8.size(); ++i)
    {
        if ((static_cast<uint8_t>(utf8[i]) & 0xc0) == 0x80)
            continue;

        if (seen++ == count)
            return utf8.substr(0, i);
    }

    return utf8;
}

std::string shared(const std::string& what,
                   const std::string& first,
                   const std::string& second)
{
    return "parameters \"" + first + "\" and \"" + second + "\" share " + what;
}
} // namespace

ParameterList::ParameterList(ParameterGroup& group, ParameterIdentity identity)
    : ids(identity)
{
    group.forEach([this](Parameter& param,
                         std::string_view id,
                         std::string_view displayName,
                         bool automatable,
                         std::span<const ParameterGroup* const> groups)
                  { add(param, id, displayName, automatable, groups); });

    findBypass();
    assignIds();
    indexHostIds();
}

void ParameterList::addHostBypass(Parameter& param)
{
    if (bypass >= 0)
        return;

    add(param, param.id(), param.name(), true, {});

    bypass = entries.size() - 1;
    entries[bypass].bypass = true;
    entries[bypass].addedBypass = true;

    assignIds();
    indexHostIds();
}

void ParameterList::setIdentity(ParameterIdentity identityToUse)
{
    ids = identityToUse;
    assignIds();
    indexHostIds();

#ifndef NDEBUG
    auto problems = identityProblems();

    for (const auto& problem: problems)
        std::fprintf(stderr, "MakeASound: %s\n", problem.c_str());

    assert(problems.empty() && "the parameters' ids clash; see identityProblems()");
#endif
}

void ParameterList::findDuplicateHostIds(Vector<std::string>& problems) const
{
    for (auto i = 1; i < byHostId.size(); ++i)
        if (byHostId[i].hostId == byHostId[i - 1].hostId)
            problems.add(shared("host id " + std::to_string(byHostId[i].hostId)
                                    + "; give one an explicit hostId",
                                entries[byHostId[i - 1].index].id,
                                entries[byHostId[i].index].id));
}

void ParameterList::add(Parameter& param,
                        std::string_view id,
                        std::string_view displayName,
                        bool automatable,
                        std::span<const ParameterGroup* const> groups)
{
    auto& entry = entries.add(Entry {});
    entry.param = &param;
    entry.id = std::string {id};
    entry.displayName = std::string {displayName};
    entry.automatable = automatable;
    entry.sessionOnly = param.isSessionOnly();
    entry.hostKey = param.hostKey();

    auto path = std::string {};

    for (const auto* group: groups)
    {
        path += (path.empty() ? "" : "/") + group->id();
        entry.groups.add({path, group->name(), path, group->id()});
    }
}

void ParameterList::assignIds()
{
    for (auto index = 0; index < entries.size(); ++index)
    {
        auto& entry = entries[index];
        auto pinned = entry.param->explicitHostId();

        switch (ids.scheme)
        {
            case IdScheme::MakeASound:
                entry.hostId = pinned.value_or(hostIdFor(entry.id));
                entry.address = entry.hostId;
                entry.identifier = entry.id;
                entry.aaxId = hexId(entry.hostId);
                break;

            case IdScheme::JUCE:
            {
                auto hash = Compat::juceStringHash(entry.hostKey);

                if (ids.juceStudioOneMask)
                    hash &= 0x7fffffffu;

                entry.hostId =
                    pinned.value_or(entry.addedBypass ? juceVst3BypassId : hash);
                entry.address = entry.addedBypass
                                    ? entry.hostId
                                    : Compat::juceStringHash64(entry.hostKey);
                entry.identifier = entry.hostKey;
                entry.aaxId = entry.param->explicitAAXId().empty()
                                  ? entry.hostKey
                                  : entry.param->explicitAAXId();
                break;
            }

            case IdScheme::JUCELegacyIndex:
                entry.hostId = pinned.value_or(static_cast<uint32_t>(index));
                entry.address = static_cast<uint64_t>(index);
                entry.identifier = entry.hostKey;
                entry.aaxId = std::to_string(index);
                break;
        }

        if (isJuceScheme(ids.scheme) && entry.bypass)
            entry.aaxId = std::string {juceAAXBypassId};

        entry.listedInAU = !(isJuceScheme(ids.scheme) && entry.addedBypass);

        for (auto& group: entry.groups)
            group.identifier = isJuceScheme(ids.scheme) ? group.ownId : group.id;
    }
}

void ParameterList::indexHostIds()
{
    byHostId.clear();
    byHostId.reserve(entries.size());
    byAddress.clear();
    byAddress.reserve(entries.size());

    for (auto i = 0; i < entries.size(); ++i)
    {
        byHostId.add(HostSlot {entries[i].hostId, i});
        byAddress.add(AddressSlot {entries[i].address, i});
    }

    std::sort(byHostId.begin(),
              byHostId.end(),
              [](const HostSlot& a, const HostSlot& b)
              { return a.hostId < b.hostId; });

    std::sort(byAddress.begin(),
              byAddress.end(),
              [](const AddressSlot& a, const AddressSlot& b)
              { return a.address < b.address; });
}

Vector<std::string> ParameterList::identityProblems() const
{
    auto problems = Vector<std::string> {};
    findDuplicateHostIds(problems);

    auto unmasked = ids.scheme == IdScheme::JUCE && !ids.juceStudioOneMask;

    for (const auto& entry: entries)
        if (auto pinned = entry.param->explicitHostId();
            !unmasked && pinned.value_or(0) >= 0x80000000u)
            problems.add("parameter \"" + entry.id + "\" pins host id "
                         + std::to_string(*pinned)
                         + ", which VST3 reserves (2^31 and up)");

    for (auto i = 1; i < byAddress.size(); ++i)
        if (byAddress[i].address == byAddress[i - 1].address)
            problems.add(shared("an AUv3 address",
                                entries[byAddress[i - 1].index].id,
                                entries[byAddress[i].index].id));

    if (!isJuceScheme(ids.scheme))
        return problems;

    // JUCE's own checks (AudioProcessor::checkForDuplicateParamID,
    // checkForDuplicateTrimmedParamID and checkForDuplicateGroupIDs), which a
    // JUCE build of the plugin would have hit too.
    auto keys = std::set<std::string> {};
    auto aaxIds = std::set<std::string> {};
    auto groupIds = std::set<std::string> {};

    for (const auto& entry: entries)
    {
        if (!keys.insert(entry.hostKey).second)
            problems.add(shared("a host key", entry.hostKey, entry.hostKey));

        if (entry.bypass)
            continue;

        if (codePoints(entry.aaxId) > juceAAXMaxIdLength)
            problems.add("parameter \"" + entry.id + "\" has the AAX id \""
                         + entry.aaxId + "\", longer than 31 characters");

        auto trimmed =
            std::string {firstCodePoints(entry.aaxId, juceAAXMaxIdLength)};

        if (!aaxIds.insert(trimmed).second)
            problems.add(shared(
                "an AAX id once truncated to 31 characters", trimmed, entry.aaxId));
    }

    // A group is listed once per entry inside it; the same path is the same
    // group, another path with the same own id a clash.
    auto groupPaths = std::set<std::string> {};

    for (const auto& entry: entries)
        for (const auto& group: entry.groups)
            if (groupPaths.insert(group.id).second
                && !groupIds.insert(group.identifier).second)
                problems.add("groups \"" + group.id
                             + "\" and another share the id \"" + group.identifier
                             + "\"");

    return problems;
}

void ParameterList::findBypass()
{
    for (auto i = 0; i < entries.size(); ++i)
    {
        if (!isHostExposed(i) || !entries[i].param->isBypass())
            continue;

        assert(bypass < 0 && "a plugin exposes one bypass parameter to the host");

        if (bypass < 0)
        {
            bypass = i;
            entries[i].bypass = true;
        }
    }
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

int ParameterList::indexOfAddress(uint64_t address) const noexcept
{
    auto found = std::lower_bound(byAddress.begin(),
                                  byAddress.end(),
                                  address,
                                  [](const AddressSlot& slot, uint64_t wanted)
                                  { return slot.address < wanted; });

    if (found == byAddress.end() || found->address != address)
        return -1;

    return found->index;
}

int ParameterList::indexOfAAXId(std::string_view aaxId) const noexcept
{
    for (auto i = 0; i < entries.size(); ++i)
        if (entries[i].aaxId == aaxId)
            return i;

    return -1;
}

bool ParameterList::isHostExposed(int index) const noexcept
{
    return index >= 0 && index < size() && entries[index].automatable;
}

bool ParameterList::isRegistered(int index) const noexcept
{
    if (isJuceScheme(ids.scheme))
        return index >= 0 && index < size();

    return isHostExposed(index);
}

} // namespace MakeASound
