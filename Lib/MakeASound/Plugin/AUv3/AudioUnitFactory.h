#pragma once

// Objective-C++ only.

#import <AudioToolbox/AudioToolbox.h>
#import <CoreAudioKit/AUViewController.h>

namespace MakeASound
{
class Plugin;
}

namespace MakeASound::AUv3
{

class Adapter;

// The module's AUAudioUnit subclass, defined by the AudioUnit.mm compiled into the
// module: what a host process registers in process, and what the extension's
// principal class instantiates.
Class audioUnitClass() noexcept;

// The adapter behind a unit of that class; null for nil or any other unit. Safe
// because the class is named per module, so only a unit of this binary passes.
Adapter* adapterOf(AUAudioUnit* unit) noexcept;

// What an extension's AUAudioUnitFactory answers: a new unit of audioUnitClass(),
// or nil with outError set when the description names no plugin of this module.
inline AUAudioUnit* createAudioUnit(AudioComponentDescription description,
                                    NSError** outError)
{
    return [[audioUnitClass() alloc] initWithComponentDescription:description
                                                            error:outError];
}

// The two below are defined by the view source the module compiles (the
// MAKEASOUND_AUV3_VIEW_SOURCE on MakeASoundAUv3), which is the choice between a
// view and none, made at link time as the v2 view's is.

// The extension's NSExtensionPrincipalClass: the view controller where there is a
// view, a plain factory where there is none. Both are AUAudioUnitFactory.
Class principalClass() noexcept;

// The indices of the host's view configurations the plugin's editor fits (see
// EmbeddedEditor::fits); every index where the module has no view.
NSIndexSet*
    supportedViewConfigurations(Plugin& plugin,
                                NSArray<AUAudioUnitViewConfiguration*>* available);

} // namespace MakeASound::AUv3
