// Compiled into each module with MAKEASOUND_AUV3_CLASS_SUFFIX defined, never into
// the static target (see ClassNames.h): the AUAudioUnit subclass a host
// instantiates, a shell forwarding every override to the format's Adapter.
#include "Adapter.h"
#include "AudioUnitFactory.h"
#include "ClassNames.h"

#define MAKEASOUND_AUV3_UNIT MAKEASOUND_AUV3_CLASS(MakeASoundAudioUnit)

using MakeASound::StateContext;
using MakeASound::AUv3::Adapter;

@interface MAKEASOUND_AUV3_UNIT : AUAudioUnit
- (instancetype)initWithComponentDescription:(AudioComponentDescription)description
                                      plugin:(const MakeASound::PluginDescription*)plugin
                                     options:
                                         (AudioComponentInstantiationOptions)options
                                       error:(NSError**)outError;
- (Adapter*)adapter;
@end

@implementation MAKEASOUND_AUV3_UNIT
{
    MakeASound::OwningPointer<Adapter> adapter;
}

- (instancetype)initWithComponentDescription:(AudioComponentDescription)description
                                     options:
                                         (AudioComponentInstantiationOptions)options
                                       error:(NSError**)outError
{
    return [self initWithComponentDescription:description
                                       plugin:nullptr
                                      options:options
                                        error:outError];
}

// `plugin` null: the module's plugin of the description's codes.
- (instancetype)initWithComponentDescription:(AudioComponentDescription)description
                                      plugin:(const MakeASound::PluginDescription*)plugin
                                     options:
                                         (AudioComponentInstantiationOptions)options
                                       error:(NSError**)outError
{
    self = [super initWithComponentDescription:description
                                       options:options
                                         error:outError];

    if (self == nil)
        return nil;

    adapter = plugin != nullptr
                  ? Adapter::create(self, description, *plugin, outError)
                  : Adapter::create(self, description, outError);
    return adapter ? self : nil;
}

- (AUAudioUnitBusArray*)inputBusses
{
    return adapter->inputBusses();
}

- (AUAudioUnitBusArray*)outputBusses
{
    return adapter->outputBusses();
}

- (NSArray<NSNumber*>*)channelCapabilities
{
    return adapter->channelCapabilities();
}

- (BOOL)shouldChangeToFormat:(AVAudioFormat*)format forBus:(AUAudioUnitBus*)bus
{
    return adapter->shouldChangeToFormat(format, bus) &&
           [super shouldChangeToFormat:format forBus:bus];
}

- (BOOL)allocateRenderResourcesAndReturnError:(NSError**)outError
{
    if (![super allocateRenderResourcesAndReturnError:outError])
        return NO;

    if (adapter->allocateRenderResources(outError))
        return YES;

    [super deallocateRenderResources];
    return NO;
}

- (void)deallocateRenderResources
{
    adapter->deallocateRenderResources();
    [super deallocateRenderResources];
}

- (AUInternalRenderBlock)internalRenderBlock
{
    return adapter->internalRenderBlock();
}

- (AUParameterTree*)parameterTree
{
    return adapter->parameterTree();
}

- (NSTimeInterval)latency
{
    return adapter->latency();
}

- (NSTimeInterval)tailTime
{
    return adapter->tailTime();
}

- (BOOL)shouldBypassEffect
{
    return adapter->shouldBypassEffect();
}

- (void)setShouldBypassEffect:(BOOL)shouldBypassEffect
{
    adapter->setShouldBypassEffect(shouldBypassEffect);
}

- (void)reset
{
    adapter->reset();
    [super reset];
}

- (NSDictionary<NSString*, id>*)fullState
{
    return adapter->fullState(StateContext::Preset);
}

- (void)setFullState:(NSDictionary<NSString*, id>*)fullState
{
    adapter->setFullState(fullState, StateContext::Preset);
}

- (NSDictionary<NSString*, id>*)fullStateForDocument
{
    return adapter->fullState(StateContext::Session);
}

- (void)setFullStateForDocument:(NSDictionary<NSString*, id>*)fullStateForDocument
{
    adapter->setFullState(fullStateForDocument, StateContext::Session);
}

- (NSArray<NSString*>*)MIDIOutputNames
{
    return adapter->midiOutputNames();
}

- (void)setContextName:(NSString*)contextName
{
    [super setContextName:contextName];
    adapter->setContextName(contextName);
}

- (NSArray<AUAudioUnitPreset*>*)factoryPresets
{
    return @[];
}

- (BOOL)supportsUserPresets
{
    return NO;
}

- (NSIndexSet*)supportedViewConfigurations:
    (NSArray<AUAudioUnitViewConfiguration*>*)availableViewConfigurations
{
    return MakeASound::AUv3::supportedViewConfigurations(
        adapter->plugin(), availableViewConfigurations);
}

- (Adapter*)adapter
{
    return adapter.get();
}

@end

Class MakeASound::AUv3::audioUnitClass() noexcept
{
    return [MAKEASOUND_AUV3_UNIT class];
}

AUAudioUnit*
    MakeASound::AUv3::createAudioUnit(AudioComponentDescription description,
                                      const PluginDescription& plugin,
                                      AudioComponentInstantiationOptions options,
                                      NSError** outError)
{
    return [[MAKEASOUND_AUV3_UNIT alloc] initWithComponentDescription:description
                                                               plugin:&plugin
                                                              options:options
                                                                error:outError];
}

Adapter* MakeASound::AUv3::adapterOf(AUAudioUnit* unit) noexcept
{
    if (![unit isKindOfClass:[MAKEASOUND_AUV3_UNIT class]])
        return nullptr;

    return [static_cast<MAKEASOUND_AUV3_UNIT*>(unit) adapter];
}
