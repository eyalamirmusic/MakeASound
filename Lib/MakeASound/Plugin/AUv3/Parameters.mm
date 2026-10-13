#include "Parameters.h"
#include "Text.h"
#include "../AU/HostParameters.h"

namespace MakeASound::AUv3
{

namespace
{
AUParameter* makeParameter(const ParameterList::Entry& entry)
{
    const auto& param = *entry.param;

    auto info = AudioUnitParameterInfo {};
    AU::fillParameterInfo(entry, info);

    if (info.cfNameString != nullptr)
        CFRelease(info.cfNameString);

    auto flags = info.flags
                 & ~(kAudioUnitParameterFlag_HasCFNameString
                     | kAudioUnitParameterFlag_CFNameRelease);

    auto* strings = static_cast<NSArray<NSString*>*>(
        CFBridgingRelease(AU::copyValueStrings(param)));

    auto* parameter =
        [AUParameterTree createParameterWithIdentifier:toNSString(entry.id)
                                                  name:toNSString(entry.displayName)
                                               address:entry.hostId
                                                   min:info.minValue
                                                   max:info.maxValue
                                                  unit:info.unit
                                              unitName:nil
                                                 flags:flags
                                          valueStrings:strings
                                   dependentParameters:nil];

    parameter.value = param.getValue();
    return parameter;
}

// One level of the tree under construction: a group's path and the children
// gathered for it so far.
struct Level
{
    const ParameterList::EnclosingGroup* group = nullptr;
    NSMutableArray<AUParameterNode*>* children = nil;
};

void closeLevel(Vector<Level>& levels)
{
    auto level = levels.back();
    levels.pop_back();

    auto* node =
        [AUParameterTree createGroupWithIdentifier:toNSString(level.group->id)
                                              name:toNSString(level.group->name)
                                          children:level.children];
    [levels.back().children addObject:node];
}

bool isOpen(const Vector<Level>& levels,
            int depth,
            const ParameterList::Entry& entry) noexcept
{
    return depth < entry.groups.size()
           && levels[depth + 1].group->id == entry.groups[depth].id;
}
} // namespace

AUParameterTree* makeParameterTree(const ParameterList& list)
{
    // levels[0] is the root; levels[d + 1] the entry's d-th enclosing group.
    auto levels = Vector<Level> {};
    levels.add({nullptr, [NSMutableArray array]});

    for (auto index = 0; index < list.size(); ++index)
    {
        if (!list.isHostExposed(index))
            continue;

        const auto& entry = list.entry(index);
        auto shared = 0;

        while (shared + 1 < levels.size() && isOpen(levels, shared, entry))
            ++shared;

        while (levels.size() > shared + 1)
            closeLevel(levels);

        for (auto depth = shared; depth < entry.groups.size(); ++depth)
            levels.add({&entry.groups[depth], [NSMutableArray array]});

        [levels.back().children addObject:makeParameter(entry)];
    }

    while (levels.size() > 1)
        closeLevel(levels);

    return [AUParameterTree createTreeWithChildren:levels.back().children];
}

} // namespace MakeASound::AUv3
