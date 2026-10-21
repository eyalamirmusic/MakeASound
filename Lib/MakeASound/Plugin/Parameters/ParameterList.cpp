#include "ParameterList.h"
#include "IdPath.h"
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

// The parameter's own key, under the separator prefixed with each enclosing
// group's own id, outermost first, each followed by it.
std::string groupedKey(const ParameterList::Entry& entry,
                       std::optional<char> separator)
{
    auto key = std::string {};

    if (separator)
    {
        for (const auto& group: entry.groups)
        {
            key += group.ownId;
            key += *separator;
        }
    }

    return key + entry.param->hostKey();
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

bool ParameterList::refreshDisplayNames()
{
    auto changed = false;

    for (auto i = 0; i < entries.size(); ++i)
    {
        auto& entry = entries[i];
        auto prefix = displayPrefixes[i];
        const auto& name = entry.param->name();

        // join()'s rule: a separator only between two non-empty parts.
        if (!prefix.empty() && name.empty() && prefix.back() == ' ')
            prefix.pop_back();

        auto displayName = prefix + name;

        if (displayName != entry.displayName)
        {
            entry.displayName = std::move(displayName);
            changed = true;
        }
    }

    return changed;
}

void ParameterList::setIdentity(ParameterIdentity identityToUse)
{
    ids = identityToUse;
    assignIds();
    indexHostIds();

#ifndef NDEBUG
    for (const auto& problem: identityProblems())
        std::fprintf(stderr, "MakeASound: %s\n", problem.c_str());

    // A JUCE port's hashed ids are a JUCE build's, which it cannot move without
    // losing automation: a clash with a MIDI controller is only reported.
    auto blocking = problemsWith(ids.paramIds != ParamIdRule::JuceKeyHash);
    assert(blocking.empty() && "the parameters' ids clash; see identityProblems()");
#endif
}

bool ParameterList::isMidiControllerId(uint32_t hostId) const noexcept
{
    if (!ids.vst3MidiControllers)
        return false;

    const auto& controllers = ids.vst3Controllers;

    if (controllers.layout == Vst3ControllerLayout::JuceContiguous)
        return hostId - controllers.base < 16u * 130u;

    return (hostId & 0xffff0000u) == controllers.base;
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

    // The groups' part of the display name, which a rename keeps.
    const auto& name = param.name();
    auto prefixLength = entry.displayName.size();

    if (entry.displayName.ends_with(name))
        prefixLength -= name.size();

    displayPrefixes.add(entry.displayName.substr(0, prefixLength));

    auto path = std::string {};

    for (const auto* group: groups)
    {
        appendIdSegment(path, group->id());
        entry.groups.add({path, group->name(), path, group->id()});
    }
}

void ParameterList::assignIds()
{
    // Under ParamIdRule::Index, the next indexed entry's index.
    auto nextIndex = 0;

    for (auto position = 0; position < entries.size(); ++position)
    {
        auto& entry = entries[position];
        entry.hostKey = groupedKey(entry, ids.juceKeyGroupSeparator);

        auto indexRule = ids.paramIds == ParamIdRule::Index;

        entry.postPort = indexRule && !entry.addedBypass
                         && entry.param->versionHint() > ids.indexedThroughVersion;

        auto index = position;

        if (indexRule && !entry.postPort)
            index = nextIndex++;

        auto pinned = entry.param->explicitHostId();
        entry.hostId = pinned.value_or(derivedHostId(entry, index));

        // A post-port entry has none of the index's ids: FNV-1a of its path
        // everywhere an index would go.
        if (entry.postPort)
        {
            entry.address = entry.hostId;
            entry.identifier =
                ids.auv3 == AUv3Ids::HostIdAndPath ? entry.id : entry.hostKey;
            entry.aaxId = hexId(entry.hostId);
            finishIds(entry);
            continue;
        }

        switch (ids.auv3)
        {
            case AUv3Ids::HostIdAndPath:
                entry.address = entry.hostId;
                entry.identifier = entry.id;
                break;

            case AUv3Ids::JuceHash64AndKey:
                entry.address = entry.addedBypass
                                    ? entry.hostId
                                    : Compat::juceStringHash64(entry.hostKey);
                entry.identifier = entry.hostKey;
                break;

            case AUv3Ids::IndexAndKey:
                entry.address = static_cast<uint64_t>(index);
                entry.identifier = entry.hostKey;
                break;
        }

        switch (ids.aaxIds)
        {
            case AAXIdRule::HexHostId:
                entry.aaxId = hexId(entry.hostId);
                break;

            case AAXIdRule::KeyOrOverride:
                entry.aaxId = entry.param->explicitAAXId().empty()
                                  ? entry.hostKey
                                  : entry.param->explicitAAXId();
                break;

            case AAXIdRule::DecimalIndex:
                entry.aaxId = std::to_string(index);
                break;
        }

        finishIds(entry);
    }
}

void ParameterList::finishIds(Entry& entry) const
{
    if (ids.aaxIds != AAXIdRule::HexHostId && entry.bypass)
        entry.aaxId = std::string {juceAAXBypassId};

    auto placement =
        entry.addedBypass ? ids.addedBypassInAU : ids.declaredBypassInAU;
    entry.listedInAU = !(entry.bypass && placement == BypassPlacement::Unlisted);

    // The enclosing groups' own ids, each followed by the separator, as
    // groupedKey() prefixes a parameter's key.
    auto joined = std::string {};

    for (auto& group: entry.groups)
    {
        switch (ids.groups)
        {
            case GroupIdRule::Path:
                group.identifier = group.id;
                break;

            case GroupIdRule::OwnId:
                group.identifier = group.ownId;
                break;

            case GroupIdRule::JoinedOwnIds:
                group.identifier = joined + group.ownId;

                if (ids.juceKeyGroupSeparator)
                    joined = group.identifier + *ids.juceKeyGroupSeparator;

                break;
        }
    }
}

uint32_t ParameterList::derivedHostId(const Entry& entry, int index) const noexcept
{
    if (entry.postPort)
        return hostIdFor(entry.id);

    if (entry.addedBypass)
    {
        switch (ids.addedBypassId)
        {
            case AddedBypassId::PathHash:
                return hostIdFor(entry.id);
            case AddedBypassId::JuceByps:
                return juceVst3BypassId;
            case AddedBypassId::Index:
                return static_cast<uint32_t>(index);
        }
    }

    switch (ids.paramIds)
    {
        case ParamIdRule::PathHash:
            break;

        case ParamIdRule::JuceKeyHash:
        {
            auto hash = Compat::juceStringHash(entry.hostKey);
            return ids.juceMask31 ? hash & 0x7fffffffu : hash;
        }

        case ParamIdRule::Index:
            return static_cast<uint32_t>(index);
    }

    return hostIdFor(entry.id);
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
    return problemsWith(true);
}

Vector<std::string> ParameterList::problemsWith(bool controllerClashes) const
{
    auto problems = Vector<std::string> {};
    findDuplicateHostIds(problems);

    // Its path cannot tell where such an id ends (IdPath.h).
    auto undelimited = std::set<std::string> {};

    for (const auto& entry: entries)
    {
        if (!isDelimitableId(entry.param->id()))
            undelimited.insert(entry.param->id());

        for (const auto& group: entry.groups)
            if (!isDelimitableId(group.ownId))
                undelimited.insert(group.ownId);
    }

    for (const auto& id: undelimited)
        problems.add("the id \"" + id
                     + "\" begins or ends with '/', which no id path can delimit");

    if (controllerClashes)
        for (const auto& entry: entries)
            if (isMidiControllerId(entry.hostId))
                problems.add("parameter \"" + entry.id + "\" has host id "
                             + std::to_string(entry.hostId)
                             + ", which a VST3 MIDI controller parameter uses; "
                               "the parameter wins");

    auto unmasked = ids.foreignHostIds
                    || (ids.paramIds == ParamIdRule::JuceKeyHash && !ids.juceMask31);

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

    if (!ids.juceChecks)
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
    if (ids.registration == Registration::All)
        return index >= 0 && index < size();

    return isHostExposed(index);
}

} // namespace MakeASound
