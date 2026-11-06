// Compiled into each <Name>-AU module, with ARC, under class names the module's
// bundle id and version make unique: the Objective-C runtime is one per process
// and every loaded .component registers its classes into it.
#include "Adapter.h"
#include "CocoaUI.h"
#include "ViewPlacement.h"
#include "../UI/GenericEditor.h"

#include <eacp/Graphics/View/View.h>
#include <eacp/Graphics/Window/EmbeddedView.h>

#import <AudioToolbox/AUCocoaUIView.h>
#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>

#include <cstdlib>

#define MAKEASOUND_AU_JOIN2(a, b) a##b
#define MAKEASOUND_AU_JOIN(a, b) MAKEASOUND_AU_JOIN2(a, b)
#define MAKEASOUND_AU_VIEW MAKEASOUND_AU_VIEW_CLASS
#define MAKEASOUND_AU_FACTORY MAKEASOUND_AU_JOIN(MAKEASOUND_AU_VIEW_CLASS, Factory)

using MakeASound::EditorSize;
using MakeASound::AU::Adapter;
using MakeASound::AU::ViewFrame;

@interface MAKEASOUND_AU_VIEW : NSView
- (instancetype)initWithAdapter:(Adapter*)adapter;
- (BOOL)resizeToWidth:(int)width height:(int)height;
- (void)closeEditor;
@end

namespace
{

// The editor's host: weak, since the view owns the editor that points here.
struct ViewHost final : MakeASound::EditorHost
{
    bool resizeEditor(EditorSize size) override
    {
        auto* strongView = view;
        return strongView != nil && [strongView resizeToWidth:size.width
                                                      height:size.height];
    }

    // Logic gives an AU window no grip, so the editor draws its own.
    bool drawsResizeHandle() const override { return false; }

    __weak MAKEASOUND_AU_VIEW* view = nil;
};

ViewFrame frameOf(NSRect rect)
{
    return {rect.origin.x, rect.origin.y, rect.size.width, rect.size.height};
}

bool isContentView(NSView* view)
{
    return view.window != nil && view == view.window.contentView;
}

bool isHostPluginContainer(NSView* view)
{
    auto* parent = view.superview;

    return parent != nil
           && MakeASound::AU::isHostPluginContainer(
               frameOf(parent.frame), isContentView(parent), frameOf(view.frame));
}

// launchctl setenv MAKEASOUND_AU_VIEW_TRACE 1 before starting the host: the
// hosts worth tracing are sandboxed and read launchd's environment.
bool isTraceEnabled()
{
    static const auto enabled = std::getenv("MAKEASOUND_AU_VIEW_TRACE") != nullptr;
    return enabled;
}

void traceViewChain(NSView* view)
{
    auto depth = 0;

    for (auto* current = view; current != nil; current = current.superview)
    {
        auto frame = current.frame;

        NSLog(@"[MakeASound AU]   %d: %@ %.0f,%.0f %.0fx%.0f%@%@",
              depth++,
              NSStringFromClass([current class]),
              frame.origin.x,
              frame.origin.y,
              frame.size.width,
              frame.size.height,
              current.isFlipped ? @" flipped" : @"",
              isContentView(current) ? @" <- window contentView" : @"");
    }
}

NSScreen* screenOf(NSWindow* window)
{
    return window.screen != nil ? window.screen : NSScreen.mainScreen;
}

// Sizing, placing and ordering the window front land over several run-loop
// turns, so one early correction is overwritten by the placement it fixes.
constexpr auto containAttempts = 12;
constexpr auto containIntervalSeconds = 0.05;

} // namespace

@implementation MAKEASOUND_AU_VIEW
{
    MakeASound::OwningPointer<MakeASound::Editor> editor;
    MakeASound::OwningPointer<eacp::Graphics::EmbeddedView> embedded;
    ViewHost viewHost;

    // Null once closeEditor ran: the adapter calls it before it goes.
    Adapter* owner;

    // Both resize directions post the same frame change; without this each
    // re-triggers the other.
    BOOL applyingResize;

    // The on-screen check is an opening correction, run once per view.
    BOOL placedWindow;
}

- (instancetype)initWithAdapter:(Adapter*)adapter
{
    auto created = adapter->plugin().createEditor();

    if (!created)
        created = EA::makeOwned<MakeASound::GenericEditor>(adapter->plugin());

    auto limits = created->constraints();
    auto size = limits.constrain(created->initialSize());
    self = [super initWithFrame:NSMakeRect(0, 0, size.width, size.height)];

    if (self == nil)
        return nil;

    editor = std::move(created);

    // A host reads the mask to decide whether its window gets a resize grip.
    self.autoresizingMask = limits.resizable
                                ? (NSViewWidthSizable | NSViewHeightSizable)
                                : NSViewNotSizable;

    embedded = EA::makeOwned<eacp::Graphics::EmbeddedView>(
        (__bridge void*) self,
        eacp::Graphics::EmbeddedViewOptions {size.width, size.height});
    embedded->setContentView(editor->view());

    viewHost.view = self;
    editor->setHost(&viewHost);
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

// A host sizes its plugin window from the view it parented, so resizing
// ourselves is the request. A host-driven resize needs no code: the mask has
// AppKit resize this view with its container, unconstrained.
- (BOOL)resizeToWidth:(int)width height:(int)height
{
    auto size = NSMakeSize(width, height);

    if (applyingResize || NSEqualSizes(size, self.frame.size))
        return YES;

    applyingResize = YES;

    [CATransaction begin];
    [CATransaction setValue:(id) kCFBooleanTrue forKey:kCATransactionDisableActions];

    auto before = self.frame;
    auto container = isHostPluginContainer(self);

    if (container)
        [self.superview setFrameSize:size];

    [self setFrameSize:size];

    [CATransaction commit];

    applyingResize = NO;

    if (isTraceEnabled())
        NSLog(@"[MakeASound AU] resize %.0fx%.0f -> %.0fx%.0f, superview %.0fx%.0f%@",
              before.size.width,
              before.size.height,
              size.width,
              size.height,
              self.superview.frame.size.width,
              self.superview.frame.size.height,
              container ? @" (container, resized)" : @" (host's, left alone)");

    // The editor has its new bounds before the drag that asked continues.
    [self layoutSubtreeIfNeeded];

    return YES;
}

// Moves the window inside the screen's visible frame when it exists to hold
// the editor. Whether the move took only the next read of the frame can tell:
// in Logic's view service the window is a stand-in and refuses every move.
- (void)containWindowTracing:(BOOL)trace
{
    auto* window = self.window;
    auto* screen = screenOf(window);

    if (window == nil || screen == nil)
        return;

    auto frame = frameOf(window.frame);
    auto visible = frameOf(screen.visibleFrame);
    auto ours = MakeASound::AU::isEditorWindow(frame, frameOf(self.frame));
    auto origin = MakeASound::AU::containedOrigin(frame, visible);
    auto moving = ours && (origin.x != frame.x || origin.y != frame.y);

    if (isTraceEnabled() && (trace || moving))
        NSLog(@"[MakeASound AU] contain %@ | window %.0f,%.0f %.0fx%.0f | visible "
              @"%.0f,%.0f %.0fx%.0f | -> %.0f,%.0f%@",
              ours ? @"(editor window)" : @"(host's window, left alone)",
              frame.x,
              frame.y,
              frame.width,
              frame.height,
              visible.x,
              visible.y,
              visible.width,
              visible.height,
              origin.x,
              origin.y,
              moving ? @" MOVING" : @"");

    if (moving)
        [window setFrameOrigin:NSMakePoint(origin.x, origin.y)];
}

// Once, from the last attempt: a shrink that re-measured would chase its own
// result down to the floor.
- (void)shrinkToFitVisibleFrame
{
    auto* window = self.window;
    auto* screen = screenOf(window);

    if (window == nil || screen == nil || !editor || !editor->canRequestResize())
        return;

    auto overhang = NSMinY(screen.visibleFrame) - NSMinY(window.frame);
    auto size = MakeASound::AU::shrinkToFit(frameOf(self.frame), overhang);

    if (isTraceEnabled() && overhang > 0.0)
        NSLog(@"[MakeASound AU] %.0f off the bottom: %@",
              overhang,
              size ? [NSString stringWithFormat:@"shrinking to %dx%d",
                                                size->width,
                                                size->height]
                   : @"shrink declined");

    if (size)
        editor->requestResize(*size);
}

- (void)containWindowAttempt:(int)attempt
{
    if (self.window == nil)
        return;

    auto isLast = attempt + 1 >= containAttempts;
    [self containWindowTracing:(attempt == 0 || isLast)];

    if (isLast)
    {
        [self shrinkToFitVisibleFrame];
        return;
    }

    __weak auto weakSelf = self;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW,
                                 static_cast<int64_t>(containIntervalSeconds
                                                      * NSEC_PER_SEC)),
                   dispatch_get_main_queue(),
                   ^{
                     [weakSelf containWindowAttempt:attempt + 1];
                   });
}

- (void)viewDidMoveToWindow
{
    [super viewDidMoveToWindow];

    if (self.window == nil || placedWindow || !editor)
        return;

    placedWindow = YES;

    if (isTraceEnabled())
    {
        NSLog(@"[MakeASound AU] parented into %@; view chain:",
              NSStringFromClass([self.window class]));
        traceViewChain(self);
    }

    [self containWindowAttempt:0];
}

- (void)closeEditor
{
    owner = nullptr;

    if (editor)
    {
        editor->onRemoved();
        editor->setHost(nullptr);
    }

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
