#pragma once

// Internal to the VST3 TUs, after VST3Common.h.

#include "../../Common/Common.h"
#include "VST3Strings.h"

#include <public.sdk/source/vst/hosting/module.h>
#include <pluginterfaces/base/ipluginbase.h>
#include <pluginterfaces/vst/ivstaudioprocessor.h>

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>

namespace MakeASound::Hosting
{

// A unicode class as the SDK's ClassInfo holds it. Not ClassInfo(PClassInfoW),
// which converts the UTF-16 fields one unit at a time through StringConvert: any
// character past U+FFFF, let alone a lone surrogate, throws there inside a
// noexcept constructor and ends the process. The 8-bit fields go through the SDK,
// which copies them as they are.
inline ::VST3::Hosting::ClassInfo classInfoOf(const Steinberg::PClassInfoW& info)
{
    static_assert(sizeof(info.category) == sizeof(Steinberg::PClassInfo2::category));
    static_assert(sizeof(info.subCategories)
                  == sizeof(Steinberg::PClassInfo2::subCategories));

    auto narrow = Steinberg::PClassInfo2();
    std::copy(std::begin(info.cid), std::end(info.cid), narrow.cid);
    narrow.cardinality = info.cardinality;
    std::copy(std::begin(info.category), std::end(info.category), narrow.category);
    std::copy(std::begin(info.subCategories),
              std::end(info.subCategories),
              narrow.subCategories);
    narrow.classFlags = info.classFlags;

    auto result = ::VST3::Hosting::ClassInfo(narrow);
    auto& data = result.get();
    data.name = VST3Strings::toUtf8(info.name);
    data.vendor = VST3Strings::toUtf8(info.vendor);
    data.version = VST3Strings::toUtf8(info.version);
    data.sdkVersion = VST3Strings::toUtf8(info.sdkVersion);
    return result;
}

// Every class a factory describes, in its order: PluginFactory::classInfos(), but
// a unicode class read through classInfoOf, and a class the factory cannot
// describe left out (the SDK's would stamp the one before it again).
inline Vector<::VST3::Hosting::ClassInfo>
    classInfosOf(const ::VST3::Hosting::PluginFactory& factory)
{
    using namespace Steinberg;

    auto result = Vector<::VST3::Hosting::ClassInfo>();
    const auto& base = factory.get();

    if (!base)
        return result;

    auto factory2 = FUnknownPtr<IPluginFactory2>(base);
    auto factory3 = FUnknownPtr<IPluginFactory3>(base);
    auto factoryVendor = std::optional<std::string>();
    const auto count = base->countClasses();

    for (auto i = int32 {0}; i < count; ++i)
    {
        auto unicode = PClassInfoW();
        auto info2 = PClassInfo2();
        auto info = PClassInfo();
        auto described = std::optional<::VST3::Hosting::ClassInfo>();

        if (factory3 && factory3->getClassInfoUnicode(i, &unicode) == kResultTrue)
            described = classInfoOf(unicode);
        else if (factory2 && factory2->getClassInfo2(i, &info2) == kResultTrue)
            described.emplace(info2);
        else if (base->getClassInfo(i, &info) == kResultTrue)
            described.emplace(info);
        else
            continue;

        // A class with no vendor of its own is the factory's.
        if (described->vendor().empty())
        {
            if (!factoryVendor)
                factoryVendor = factory.info().vendor();

            described->get().vendor = *factoryVendor;
        }

        result.add(std::move(*described));
    }

    return result;
}

// A factory's audio-effect classes, the only kind a host instantiates, in factory
// order. The scanner and the loader both read the module through this, so they
// agree on what "the classes in this module" means.
inline Vector<::VST3::Hosting::ClassInfo>
    audioClassesOf(const ::VST3::Hosting::PluginFactory& factory)
{
    auto result = Vector<::VST3::Hosting::ClassInfo>();

    for (auto& info: classInfosOf(factory))
        if (info.category() == kVstAudioEffectClass)
            result.add(std::move(info));

    return result;
}

// The class UID as PluginDescription::classId spells it: 32 hex digits. One
// spelling on both sides, so a cached id always matches at load time.
inline std::string classIdOf(const ::VST3::Hosting::ClassInfo& audioClass)
{
    return audioClass.ID().toString();
}

} // namespace MakeASound::Hosting
