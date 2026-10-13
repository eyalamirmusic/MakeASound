#pragma once

#include "../HostFormat.h"

namespace MakeASound::Hosting
{

// VST3, as a host sees it. A target is a .vst3 bundle (or, on Windows, a bare
// .vst3 DLL); its classes are the factory's audio-effect classes, each with its
// class UID as 32 hex digits for an id. The SDK stays in the implementation.
class VST3Format : public HostFormat
{
public:
    PluginFormat format() const override;

    // The platform's VST3 folders that exist (VST3Folders-<os>.cpp):
    //
    //   macOS    ~/Library/Audio/Plug-Ins/VST3, /Library/Audio/Plug-Ins/VST3
    //   Windows  %COMMONPROGRAMFILES%\VST3, %LOCALAPPDATA%\Programs\Common\VST3
    //
    // Folders, not the SDK's Module::getModulePaths(), which answers with the
    // modules it found: a database that recorded those as where it looked would
    // forget where to look as soon as a plugin moved.
    Vector<std::string> defaultSearchFolders() const override;

    Vector<std::string>
        findTargets(const Vector<std::string>& folders) const override;

    // Anything named *.vst3, whatever its case.
    bool ownsTarget(std::string_view target) const override;

    // Loads the module and describes each audio-effect class: name, vendor,
    // version, categories, channel counts of the default arrangement, MIDI input
    // and whether it has an editor. Each class is instantiated just long enough
    // to ask, and its view is created and released without being attached.
    ModuleScan scan(std::string_view target) const override;
};

} // namespace MakeASound::Hosting
