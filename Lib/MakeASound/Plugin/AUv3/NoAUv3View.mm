// The view source of a module built without eacp's UI tier, compiled into each
// module as AUv3View-<platform>.mm would be: the principal class is a plain
// AUAudioUnitFactory, which an extension declared as com.apple.AudioUnit (not
// -UI) names, and every view configuration is answered, there being no editor
// to fit.
#include "AudioUnitFactory.h"
#include "ClassNames.h"

#define MAKEASOUND_AUV3_FACTORY MAKEASOUND_AUV3_CLASS(MakeASoundAUv3Factory)

@interface MAKEASOUND_AUV3_FACTORY : NSObject <AUAudioUnitFactory>
@end

@implementation MAKEASOUND_AUV3_FACTORY

- (void)beginRequestWithExtensionContext:(NSExtensionContext*)context
{
}

- (AUAudioUnit*)createAudioUnitWithComponentDescription:
                    (AudioComponentDescription)description
                                                  error:(NSError**)outError
{
    return MakeASound::AUv3::createAudioUnit(description, outError);
}

@end

Class MakeASound::AUv3::principalClass() noexcept
{
    return [MAKEASOUND_AUV3_FACTORY class];
}

NSIndexSet* MakeASound::AUv3::supportedViewConfigurations(
    Plugin&, NSArray<AUAudioUnitViewConfiguration*>* available)
{
    return [NSIndexSet indexSetWithIndexesInRange:NSMakeRange(0, available.count)];
}
