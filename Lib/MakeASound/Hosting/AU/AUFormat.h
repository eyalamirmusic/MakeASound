#pragma once

#include "../HostFormat.h"

#include <string_view>

namespace MakeASound::Hosting
{

// Audio Units (macOS), as a host sees them: v2 components, and v3 extensions
// through the v2 API, which reaches an out-of-process AUv3 over XPC.
//
// An Audio Unit is a REGISTRY ENTRY, not a file. Its target is the
// (type, subtype, manufacturer) triple of its AudioComponentDescription written
// as three four-character codes joined by colons, "aumu:Synt:EAPM", and the same
// string is its PluginDescription::classId, since a component is exactly one
// class. Only the hostable types are swept: 'aufx' effects, 'aumf' music effects,
// 'aumu' instruments and 'aumi' MIDI processors.
//
// A component stamps as its VERSION (AudioComponentGetVersion, in lastModified,
// with fileSize zero), which changes exactly when the plugin is updated. Stamping
// it as a file would read every cached Audio Unit as stale forever, and a crashed
// one would never stay blacklisted; stamping it {0, 0} would never rescan an
// update. A cache shared with other formats must stamp through FormatManager.
//
// The AudioToolbox API stays in the implementation.
class AUFormat : public HostFormat
{
public:
    PluginFormat format() const override;

    // Empty: the registry is not a folder.
    Vector<std::string> defaultSearchFolders() const override;

    // Every hostable component the registry answers with, deduplicated and
    // sorted. The folders are ignored.
    Vector<std::string>
        findTargets(const Vector<std::string>& folders) const override;

    // Spelled like an identifier (looksLikeAudioUnitIdentifier).
    bool ownsTarget(std::string_view target) const override;

    // Whether the registry answers to the identifier. Instantiates nothing.
    bool targetExists(std::string_view target) const override;

    // The component's version; {0, 0} for one that is not registered.
    ModuleStamp stamp(std::string_view target) const override;

    // Describes the component as one class. The sub-categories read like the
    // VST3 vocabulary, so isInstrument() agrees with the type: 'aumu'
    // "Instrument", 'aufx' "Fx", 'aumf' "Fx|MusicEffect", 'aumi' "Fx|MidiEffect".
    // The component is instantiated (asynchronously, pumping the event loop, for
    // up to 15 s) to read its channel counts and whether it has a view of its own,
    // and is never initialized. A component that will not instantiate is a clean
    // error with no class.
    ModuleScan scan(std::string_view target) const override;

    // Finds the component the description's class id names and loads it
    // (loadAudioUnit in HostedAudioUnit.h).
    LoadResult load(const PluginDescription& description,
                    const HostInfo& host) const override;
};

// The spelling of an Audio Unit identifier: exactly three four-character tokens
// of printable ASCII, other than ':', joined by colons. This is what tells an
// identifier from a file path, which may hold colons but also slashes. A code may
// hold spaces ("aumu:dls :appl").
bool looksLikeAudioUnitIdentifier(std::string_view text) noexcept;

} // namespace MakeASound::Hosting
