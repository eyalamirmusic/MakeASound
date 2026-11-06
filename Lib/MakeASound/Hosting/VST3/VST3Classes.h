#pragma once

// Internal to the VST3 TUs, after VST3Common.h.

#include "../../Common/Common.h"

#include <public.sdk/source/vst/hosting/module.h>
#include <pluginterfaces/vst/ivstaudioprocessor.h>

#include <string>
#include <string_view>

namespace MakeASound::Hosting
{

// A factory's audio-effect classes, the only kind a host instantiates, in factory
// order. The scanner and the loader both read the module through this, so they
// agree on what "the classes in this module" means.
inline Vector<::VST3::Hosting::ClassInfo>
    audioClassesOf(const ::VST3::Hosting::PluginFactory& factory)
{
    auto result = Vector<::VST3::Hosting::ClassInfo>();

    for (auto& info: factory.classInfos())
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
