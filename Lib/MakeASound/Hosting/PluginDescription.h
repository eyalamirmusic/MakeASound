#pragma once

#include "Text.h"
#include "../Common/Common.h"
#include "../Plugin/Core/Plugin.h"

// ReflectEnum.h as well as ReflectMacro.h: PluginFormat reflects by enumerator
// name, which needs the enum support Serialize.h does not pull in.
#include <Miro/Reflection/ReflectEnum.h>
#include <Miro/Reflection/ReflectMacro.h>

#include <cstdint>
#include <string>

namespace MakeASound::Hosting
{

// The format of a scanned plugin is the framework's own PluginFormat. A scanned
// plugin is VST3 or AU (an AUv3 is reached through the v2 API, so it scans as AU);
// the other enumerators never appear here.
//
// Miro writes an enum as the enumerator's NAME, so the cache stays readable when
// a format is inserted ahead of an existing one. "VST3" and "AU" are the names
// Tamber Plug's cache used, which is what keeps its files loading here.

// The identity stamp of a scan target. Two targets at the same path with the
// same stamp are taken to be the same build, so the cache can skip rescanning
// them and keep a failed one blacklisted.
//
// A file-backed module stamps as its binaries' newest mtime and total size (see
// moduleStamp). An Audio Unit has no file and stamps as its component version in
// `lastModified`, with `fileSize` zero. Compare two stamps; never print one as a
// date.
struct ModuleStamp
{
    std::int64_t lastModified = 0;
    std::int64_t fileSize = 0;

    bool operator==(const ModuleStamp& other) const = default;
};

// One scanned plugin class. A module can expose several, so identity is the
// module path PLUS the class id, never a position in the module's class list.
//
// The field names are the cache's schema and are Tamber Plug's PluginInfo names
// verbatim. The schema is append-only: never rename or repurpose a field.
struct PluginDescription
{
    MIRO_REFLECT(format,
                 path,
                 classId,
                 name,
                 vendor,
                 version,
                 category,
                 subCategories,
                 numAudioInputs,
                 numAudioOutputs,
                 hasMidiInput,
                 hasEditor)

    PluginFormat format = PluginFormat::VST3;

    // The scan target: a module bundle (or a bare .vst3 DLL on Windows), or for
    // an Audio Unit its "type:subtype:manufacturer" identifier.
    std::string path;

    // The format's stable class identity: the VST3 class UID as 32 hex digits,
    // or for an Audio Unit the same identifier as `path`, since a component is
    // exactly one class.
    std::string classId;

    std::string name;
    std::string vendor;
    std::string version;

    // The format's class category ("Audio Module Class" for VST3, the component
    // type for an AU) and the '|'-separated sub-categories ("Instrument|Synth").
    std::string category;
    std::string subCategories;

    // Total channels across every audio input / output bus of the default
    // arrangement.
    int numAudioInputs = 0;
    int numAudioOutputs = 0;

    bool hasMidiInput = false;
    bool hasEditor = false;

    // JUCE's rule, a case-insensitive substring match over the sub-categories, so
    // a plugin classifies the same here as in a JUCE host.
    bool isInstrument() const
    {
        return Text::containsIgnoringCase(subCategories, "Instrument");
    }
};

// What scanning one target produced: its stamp (so the cache can tell when to
// rescan), the classes it exposed, and how it failed if it did.
//
// `error` is a failure the plugin reported cleanly. `crashed` means the scan
// never answered at all: the worker died, timed out or printed nothing usable.
// Either kind stays blacklisted until the target's stamp changes.
struct ModuleScan
{
    MIRO_REFLECT(format, path, lastModified, fileSize, classes, error, crashed)

    // The format, known even when the scan failed and there is no class to read
    // it from. The cache stamps an entry by its format, and a format-specific
    // sweep evicts only its own entries. A file written before the field existed
    // decodes as VST3, which is what those entries were.
    PluginFormat format = PluginFormat::VST3;

    std::string path;
    std::int64_t lastModified = 0;
    std::int64_t fileSize = 0;
    Vector<PluginDescription> classes;
    std::string error;
    bool crashed = false;

    bool succeeded() const { return error.empty() && !crashed; }

    ModuleStamp stamp() const { return {lastModified, fileSize}; }

    void setStamp(const ModuleStamp& stampToUse)
    {
        lastModified = stampToUse.lastModified;
        fileSize = stampToUse.fileSize;
    }
};

} // namespace MakeASound::Hosting
