#pragma once

#include "Parameter.h"
#include "../../Common/Common.h"

#include <Miro/Miro.h>

#include <cassert>
#include <concepts>
#include <functional>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>

namespace MakeASound
{

struct GroupOptions
{
    // The group's key in saved state and its segment of every id below it; the
    // name when empty.
    std::string_view id = {};

    // False hides every parameter below from the host, whatever its own flag.
    bool automatable = true;

    // False keeps every parameter below out of saved state, whatever its own
    // flag: no key for the group is written in any context, and a load never
    // reads one or resets anything below, so a document holding its key moves
    // nothing. The parameters stay registered as in any other group (ids,
    // index, host visibility), which is what keeps retired padding holding the
    // ids of the parameters after it.
    bool persistent = true;

    // ParameterOptions::reportsPluginMoves for every parameter below that makes
    // no choice of its own, unless a nested group makes another. Unset passes
    // the enclosing group's down.
    std::optional<bool> reportsPluginMoves = {};
};

// A set of parameters and nested groups, registered with add() in the
// constructor. Registration order is declaration order and automation order. It
// holds pointers to its members, so it neither copies nor moves.
class ParameterGroup
{
public:
    // `groups` are the named groups enclosing the parameter, outermost first.
    // `automatable` and `persistent` are the AND of its flag and its groups'.
    using Visitor =
        std::function<void(Parameter& param,
                           std::string_view id,
                           std::string_view displayName,
                           bool automatable,
                           bool persistent,
                           std::span<const ParameterGroup* const> groups)>;

    // What a Visitor is given, and the parameter's resolved
    // ParameterOptions::reportsPluginMoves: its own, else the innermost
    // enclosing group's, nameless ones included; unset when none chose.
    struct Member
    {
        Parameter& param;
        std::string_view id;
        std::string_view displayName;
        bool automatable;
        bool persistent;
        std::optional<bool> reportsPluginMoves;
        std::span<const ParameterGroup* const> groups;
    };

    using MemberVisitor = std::function<void(const Member& member)>;

    explicit ParameterGroup(std::string_view nameToUse = {},
                            GroupOptions options = {});
    virtual ~ParameterGroup() = default;

    ParameterGroup(const ParameterGroup&) = delete;
    ParameterGroup& operator=(const ParameterGroup&) = delete;

    const std::string& name() const noexcept { return groupName; }
    const std::string& id() const noexcept { return groupId; }
    bool isAutomatable() const noexcept { return automatable; }
    bool isPersistent() const noexcept { return persistent; }

    // Its own choice, unset when it made none.
    std::optional<bool> reportsPluginMoves() const noexcept { return moveReports; }

    // Every parameter below, depth-first, with its id path ("Osc 1/Attack", a
    // '/' inside one id doubled: see IdPath.h),
    // display path ("Osc 1 Attack") and the AND of its flags and its groups'.
    void forEach(const Visitor& visitor) const;

    // The same walk, every resolved flag in one Member.
    void forEachMember(const MemberVisitor& visitor) const;

    // Each parameter under its id, each named group as a sub-object. A load
    // assigns into the registered parameters and restores the default of one
    // whose key is missing or holds nothing it can read. A group or parameter
    // that is not persistent is neither written nor read.
    void reflect(Miro::Reflector& ref);

protected:
    // Constructor-time only: a ParameterList built earlier would not see it.
    // Takes parameters, groups, pointers to either, and ranges of any of those.
    template <typename... Items>
    void add(Items&... items)
    {
        (addItem(items), ...);
    }

    // Adds `group` under a name the parent gives it, replacing the one it gave
    // itself: how one group type is mounted more than once, an array of them
    // included ("Osc 1", "Osc 2"). `options.id` is its key and id segment, the
    // name when empty, and `options.automatable` and `options.persistent` AND
    // with the group's own; `options.reportsPluginMoves`, when set, replaces the
    // group's own. Constructor-time only, as add().
    void addChild(std::string_view nameToUse,
                  ParameterGroup& group,
                  GroupOptions options = {});

private:
    struct Entry
    {
        Parameter* param = nullptr;
        ParameterGroup* group = nullptr;
    };

    template <typename T>
    void addItem(T& item)
    {
        if constexpr (std::derived_from<T, Parameter>)
            addParameter(item);
        else if constexpr (std::derived_from<T, ParameterGroup>)
            addGroup(item);
        else if constexpr (std::ranges::range<T>)
        {
            for (auto& element: item)
                addItem(element);
        }
        else if constexpr (requires { *item; })
        {
            assert(item != nullptr && "an empty pointer in a parameter group");
            addItem(*item);
        }
        else
            static_assert(sizeof(T) == 0, "add() takes parameters and groups");
    }

    void addParameter(Parameter& param);
    void addGroup(ParameterGroup& group);

    // A nameless child shares this group's key space.
    bool hasKey(std::string_view key) const;

    void walk(const MemberVisitor& visitor,
              const std::string& idPrefix,
              const std::string& displayPrefix,
              bool inheritedAutomatable,
              bool inheritedPersistent,
              std::optional<bool> inheritedMoveReports,
              Vector<const ParameterGroup*>& enclosing) const;

    void reflectEntries(Miro::Reflector& ref, bool session);
    void resetToDefaults(bool session) noexcept;

    std::string groupName;
    std::string groupId;
    bool automatable;
    bool persistent;
    std::optional<bool> moveReports;
    Vector<Entry> entries;
};

} // namespace MakeASound
