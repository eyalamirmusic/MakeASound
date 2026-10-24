#include "../Plugin/VST3/VST3Common.h"
#include "JuceHostCompat.h"

#include <public.sdk/source/vst/utility/uid.h>

#include <array>
#include <optional>
#include <string_view>

namespace MakeASound::Compat
{

namespace
{
bool isHexDigit(char c) noexcept
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')
           || (c >= 'A' && c <= 'F');
}

// UID::fromString checks only the length.
std::optional<::VST3::UID> vst3ClassId(std::string_view text)
{
    if (text.size() != 32)
        return std::nullopt;

    for (auto c: text)
        if (!isHexDigit(c))
            return std::nullopt;

    auto uid = ::VST3::UID::fromString(std::string {text});

    if (!uid)
        return std::nullopt;

    return *uid;
}

std::optional<uint32_t> fourCharCode(std::string_view text) noexcept
{
    if (text.size() != 4)
        return std::nullopt;

    auto code = uint32_t {0};

    for (auto c: text)
        code = (code << 8) | static_cast<uint8_t>(c);

    return code;
}

// type ^ subtype ^ manufacturer of "tttt:ssss:mmmm".
int32_t audioUnitId(std::string_view classId) noexcept
{
    if (classId.size() != 14 || classId[4] != ':' || classId[9] != ':')
        return 0;

    auto type = fourCharCode(classId.substr(0, 4));
    auto subtype = fourCharCode(classId.substr(5, 4));
    auto manufacturer = fourCharCode(classId.substr(10, 4));

    if (!type || !subtype || !manufacturer)
        return 0;

    return static_cast<int32_t>(*type ^ *subtype ^ *manufacturer);
}

// One group of the tree: its children in creation order, each a listed
// position or a sub-group.
struct GroupNode
{
    struct Child
    {
        int position = -1;
        int64_t groupId = 0;
        OwningPointer<GroupNode> group;
    };

    GroupNode& subGroup(int64_t id)
    {
        for (auto& child: children)
            if (child.group != nullptr && child.groupId == id)
                return *child.group;

        auto& child = children.create();
        child.groupId = id;
        child.group = EA::makeOwned<GroupNode>();
        return *child.group;
    }

    void flatten(Vector<int>& out) const
    {
        for (const auto& child: children)
        {
            if (child.group != nullptr)
                child.group->flatten(out);
            else
                out.add(child.position);
        }
    }

    Vector<Child> children;
};

// getHashForRange.
template <typename Range>
int32_t hashOf(const Range& range) noexcept
{
    auto value = uint32_t {0};

    for (const auto& item: range)
        value = value * 31 + static_cast<uint32_t>(item);

    return static_cast<int32_t>(value);
}
} // namespace

int32_t juceHostedUniqueId(const Hosting::PluginDescription& description)
{
    switch (description.format)
    {
        case PluginFormat::VST3:
        {
            auto uid = vst3ClassId(description.classId);

            if (!uid)
                return 0;

            auto fuid = Steinberg::FUID::fromTUID(uid->data());
            auto words = std::array<uint32_t, 4> {
                fuid.getLong1(), fuid.getLong2(), fuid.getLong3(), fuid.getLong4()};
            return hashOf(words);
        }

        case PluginFormat::AU:
            return audioUnitId(description.classId);

        default:
            return 0;
    }
}

int32_t juceHostedDeprecatedUid(const Hosting::PluginDescription& description)
{
    switch (description.format)
    {
        case PluginFormat::VST3:
        {
            auto uid = vst3ClassId(description.classId);

            if (!uid)
                return 0;

            // TUID is char[16]: JUCE's (uint32) item sign-extends.
            auto bytes = std::array<int32_t, 16> {};

            for (auto i = size_t {0}; i < bytes.size(); ++i)
                bytes[i] = static_cast<signed char>(uid->data()[i]);

            return hashOf(bytes);
        }

        case PluginFormat::AU:
            return audioUnitId(description.classId);

        default:
            return 0;
    }
}

Vector<int> juceParameterTreeOrder(const Vector<Vector<int64_t>>& groupPaths)
{
    auto root = GroupNode();

    for (auto position = 0; position < groupPaths.size(); ++position)
    {
        auto* node = &root;

        for (auto id: groupPaths[position])
            node = &node->subGroup(id);

        node->children.create().position = position;
    }

    auto order = Vector<int>();
    root.flatten(order);
    return order;
}

} // namespace MakeASound::Compat
