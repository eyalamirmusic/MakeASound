// Compiled into each <Name>-AU module, with ARC, under class names the module's
// bundle id and version make unique: the Objective-C runtime is one per process
// and every loaded .component registers its classes into it.
#include "Adapter.h"
#include "CocoaUI.h"
#include "../UI/GenericEditor.h"

#include <eacp/Graphics/View/View.h>
#include <eacp/Graphics/Window/EmbeddedView.h>

#import <AudioToolbox/AUCocoaUIView.h>
#import <Cocoa/Cocoa.h>

#define MAKEASOUND_AU_JOIN2(a, b) a##b
#define MAKEASOUND_AU_JOIN(a, b) MAKEASOUND_AU_JOIN2(a, b)
#define MAKEASOUND_AU_VIEW MAKEASOUND_AU_VIEW_CLASS
#define MAKEASOUND_AU_FACTORY MAKEASOUND_AU_JOIN(MAKEASOUND_AU_VIEW_CLASS, Factory)

using MakeASound::AU::Adapter;

@interface MAKEASOUND_AU_VIEW : NSView
- (instancetype)initWithAdapter:(Adapter*)adapter;
- (void)closeEditor;
@end

@implementation MAKEASOUND_AU_VIEW
{
    MakeASound::OwningPointer<MakeASound::Editor> editor;
    MakeASound::OwningPointer<eacp::Graphics::EmbeddedView> embedded;

    // Null once closeEditor ran: the adapter calls it before it goes.
    Adapter* owner;
}

- (instancetype)initWithAdapter:(Adapter*)adapter
{
    auto created = adapter->plugin().createEditor();

    if (!created)
        created = EA::makeOwned<MakeASound::GenericEditor>(adapter->plugin());

    auto size = created->initialSize();
    self = [super initWithFrame:NSMakeRect(0, 0, size.width, size.height)];

    if (self == nil)
        return nil;

    editor = std::move(created);

    // A host reads the mask to decide whether its window gets a resize grip.
    self.autoresizingMask = editor->isResizable()
                                ? (NSViewWidthSizable | NSViewHeightSizable)
                                : NSViewNotSizable;

    embedded = EA::makeOwned<eacp::Graphics::EmbeddedView>(
        (__bridge void*) self,
        eacp::Graphics::EmbeddedViewOptions {size.width, size.height});
    embedded->setContentView(editor->view());
    editor->onAttached();

    // The host owns this view past the unit (Logic disposes the unit first), so
    // the adapter closes it while the plugin still lives. Weak: the adapter
    // holding this view strongly would keep it from ever being freed.
    owner = adapter;
    __weak auto weakSelf = self;
    adapter->addViewCloser((__bridge void*) self,
                           [weakSelf] { [weakSelf closeEditor]; });

    return self;
}

- (void)closeEditor
{
    owner = nullptr;

    if (editor)
        editor->onRemoved();

    embedded.reset();
    editor.reset();
}

- (void)dealloc
{
    if (owner != nullptr)
        owner->removeViewCloser((__bridge void*) self);

    [self closeEditor];
}

@end

@interface MAKEASOUND_AU_FACTORY : NSObject <AUCocoaUIBase>
@end

@implementation MAKEASOUND_AU_FACTORY

- (unsigned)interfaceVersion
{
    return 0;
}

- (NSView*)uiViewForAudioUnit:(AudioUnit)unit withSize:(NSSize)preferredSize
{
    (void) preferredSize;

    auto* adapter = static_cast<Adapter*>(nullptr);
    auto size = static_cast<UInt32>(sizeof(adapter));

    if (AudioUnitGetProperty(unit,
                             MakeASound::AU::adapterProperty,
                             kAudioUnitScope_Global,
                             0,
                             &adapter,
                             &size)
            != noErr
        || adapter == nullptr)
        return nil;

    try
    {
        return [[MAKEASOUND_AU_VIEW alloc] initWithAdapter:adapter];
    }
    catch (...)
    {
        return nil;
    }
}

@end

namespace MakeASound::AU
{

CocoaViewInfo cocoaViewInfo()
{
    auto* factory = [MAKEASOUND_AU_FACTORY class];
    auto* bundle = [NSBundle bundleForClass:factory];

    return {static_cast<CFStringRef>(CFBridgingRetain(NSStringFromClass(factory))),
            static_cast<CFURLRef>(CFBridgingRetain(bundle.bundleURL))};
}

} // namespace MakeASound::AU
