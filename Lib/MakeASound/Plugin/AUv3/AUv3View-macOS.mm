// Compiled into each module with MAKEASOUND_AUV3_CLASS_SUFFIX defined, never into
// a static target (see ClassNames.h): the extension's principal class over
// AppKit, which macOS ships no AUv3 with but runs the AUv3 suites on. The UIKit
// twin is AUv3View-iOS.mm; EmbeddedEditor holds what the two share.
#include "AudioUnitFactory.h"
#include "ClassNames.h"
#include "EmbeddedEditor.h"

#import <Cocoa/Cocoa.h>

#include <cmath>

#define MAKEASOUND_AUV3_VIEW_CONTROLLER                                            \
    MAKEASOUND_AUV3_CLASS(MakeASoundAUv3ViewController)

using MakeASound::EditorSize;
using MakeASound::AUv3::EmbeddedEditor;

namespace
{
EditorSize sizeOf(NSSize size)
{
    return {static_cast<int>(std::lround(size.width)),
            static_cast<int>(std::lround(size.height))};
}
} // namespace

@interface MAKEASOUND_AUV3_VIEW_CONTROLLER : AUViewController <AUAudioUnitFactory>
@end

@implementation MAKEASOUND_AUV3_VIEW_CONTROLLER
{
    // The host keeps the unit; the view may outlive it, and closes through the
    // adapter's closer when it goes.
    __weak AUAudioUnit* unit;
    MakeASound::OwningPointer<EmbeddedEditor> editor;
}

// May be called on a thread of the host's; the editor is the main thread's.
- (AUAudioUnit*)createAudioUnitWithComponentDescription:
                    (AudioComponentDescription)description
                                                  error:(NSError**)outError
{
    auto* created = MakeASound::AUv3::createAudioUnit(description, outError);

    if (created == nil)
        return nil;

    unit = created;

    if (NSThread.isMainThread)
    {
        [self openEditor];
    }
    else
    {
        __weak auto weakSelf = self;
        dispatch_async(dispatch_get_main_queue(), ^{
          [weakSelf openEditor];
        });
    }

    return created;
}

// No nib: AppKit's own loadView would look for one.
- (void)loadView
{
    self.view = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 400, 300)];
}

// Once both the view and the unit exist, whichever came last.
- (void)openEditor
{
    if (editor != nullptr || !self.isViewLoaded)
        return;

    auto* adapter = MakeASound::AUv3::adapterOf(unit);

    if (adapter == nullptr)
        return;

    editor = EA::makeOwned<EmbeddedEditor>(*adapter);

    auto opening = editor->openingSize();
    auto preferred = NSMakeSize(opening.width, opening.height);
    self.preferredContentSize = preferred;

    // Until a host has the view it is ours to size.
    if (self.view.superview == nil)
        [self.view setFrameSize:preferred];

    __weak auto weakSelf = self;
    editor->attach((__bridge void*) self.view,
                   sizeOf(self.view.bounds.size),
                   [weakSelf](EditorSize size)
                   {
                       auto* controller = weakSelf;

                       if (controller == nil)
                           return false;

                       controller.preferredContentSize =
                           NSMakeSize(size.width, size.height);
                       return true;
                   });
}

- (void)viewDidLoad
{
    [super viewDidLoad];
    [self openEditor];
}

- (void)viewDidLayout
{
    [super viewDidLayout];

    if (editor != nullptr)
        editor->hostResized(sizeOf(self.view.bounds.size));
}

@end

Class MakeASound::AUv3::principalClass() noexcept
{
    return [MAKEASOUND_AUV3_VIEW_CONTROLLER class];
}

NSIndexSet* MakeASound::AUv3::supportedViewConfigurations(
    Plugin& plugin, NSArray<AUAudioUnitViewConfiguration*>* available)
{
    return EmbeddedEditor::supportedConfigurations(plugin, available);
}
