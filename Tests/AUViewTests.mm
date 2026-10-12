// The AU Cocoa view's size policy and Logic's view-service heuristics: the pure
// placement geometry, then the view itself, opened through the test module's
// factory class the way a host opens it, with no adapter header.

#include "AUTestHost.h"

#include <MakeASound/Plugin/AU/ViewPlacement.h>

#include <NanoTest/NanoTest.h>

#include <eacp/Graphics/View/View.h>

#import <AudioToolbox/AUCocoaUIView.h>
#import <Cocoa/Cocoa.h>

#include <memory>

using namespace nano;
using namespace TestPlugins;
using namespace AUHost;
using namespace MakeASound::AU;

namespace
{
// What happened to a ProbeEditor, readable after the view destroyed it.
struct EditorRecord
{
    EditorSize size {400, 300};
    EditorConstraints limits;

    Editor* editor = nullptr;
    eacp::Graphics::View* content = nullptr;
    EditorHost* hostAtAttach = nullptr;
    EditorHost* hostAtRemove = nullptr;
    int attached = 0;
    int removed = 0;
    bool destroyed = false;
};

class ProbeEditor final : public Editor
{
public:
    explicit ProbeEditor(EditorRecord& recordToUse)
        : record(recordToUse)
    {
        record.editor = this;
    }

    ~ProbeEditor() override
    {
        record.editor = nullptr;
        record.content = nullptr;
        record.destroyed = true;
    }

    eacp::Graphics::View& view() override
    {
        if (content == nullptr)
        {
            content = EA::makeOwned<eacp::Graphics::View>();
            record.content = content.get();
        }

        return *content;
    }

    EditorSize initialSize() const override { return record.size; }
    EditorConstraints constraints() const override { return record.limits; }

    void onAttached() override
    {
        ++record.attached;
        record.hostAtAttach = host();
    }

    void onRemoved() override
    {
        ++record.removed;
        record.hostAtRemove = host();
    }

private:
    EditorRecord& record;
    OwningPointer<eacp::Graphics::View> content;
};

// The test plugins show a ProbeEditor over the record while this lives.
struct ScopedProbe
{
    ScopedProbe()
    {
        Module::editorFactory = [this](Plugin&)
        { return OwningPointer<Editor>(EA::makeOwned<ProbeEditor>(record)); };
    }

    ~ScopedProbe() { Module::editorFactory = nullptr; }

    ScopedProbe(const ScopedProbe&) = delete;
    ScopedProbe& operator=(const ScopedProbe&) = delete;

    EditorRecord record;
};

EditorConstraints resizableBetween(EditorSize minimum, EditorSize maximum)
{
    return {.resizable = true, .minimum = minimum, .maximum = maximum};
}

// The view a host gets: the class kAudioUnitProperty_CocoaUI names, asked for a
// view of the unit.
NSView* openView(AudioUnit unit)
{
    auto info = AudioUnitCocoaViewInfo {};
    auto size = UInt32 {sizeof(info)};

    if (AudioUnitGetProperty(
            unit, kAudioUnitProperty_CocoaUI, kAudioUnitScope_Global, 0, &info, &size)
        != noErr)
        return nil;

    auto* className = (NSString*) CFBridgingRelease(info.mCocoaAUViewClass[0]);
    CFRelease(info.mCocoaAUViewBundleLocation);

    auto factoryClass = NSClassFromString(className);

    if (factoryClass == nil)
        return nil;

    auto factory = (id<AUCocoaUIBase>) [[factoryClass alloc] init];
    return [factory uiViewForAudioUnit:unit withSize:NSZeroSize];
}

NSSize sizeOf(NSView* view)
{
    return view.frame.size;
}

bool hasSize(NSView* view, double width, double height)
{
    return NSEqualSizes(sizeOf(view), NSMakeSize(width, height));
}

NSView* contentOf(const EditorRecord& record)
{
    return (__bridge NSView*) record.content->getHandle();
}

auto tEditorWindow = test("AU/anEditorsWindowIsOneWithLittleChrome") = []
{
    auto view = ViewFrame {0, 0, 600, 400};

    check(isEditorWindow({100, 100, 600, 428}, view));
    check(isEditorWindow({100, 100, 680, 600}, view));
    check(!isEditorWindow({0, 0, 1600, 1000}, view));
    check(!isEditorWindow({0, 0, 600, 601}, view));
};

auto tPluginContainer = test("AU/aSameSizeSuperviewThatIsNoContentViewIsTheContainer") =
    []
{
    auto view = ViewFrame {0, 0, 600, 400};

    check(isHostPluginContainer({20, 30, 600, 400}, false, view));
    check(!isHostPluginContainer({0, 0, 600, 400}, true, view));
    check(!isHostPluginContainer({0, 0, 600, 440}, false, view));
};

auto tContainedOrigin = test("AU/aWindowHangingOffAnEdgeMovesInsideTheVisibleFrame") =
    []
{
    auto visible = ViewFrame {0, 25, 1440, 850};
    auto moved = [&](ViewFrame window)
    {
        auto origin = containedOrigin(window, visible);
        return std::pair {origin.x, origin.y};
    };

    check(moved({100, 200, 600, 400}) == std::pair {100.0, 200.0});
    check(moved({100, -150, 600, 400}) == std::pair {100.0, 25.0});
    check(moved({100, 700, 600, 400}) == std::pair {100.0, 475.0});
    check(moved({-80, 200, 600, 400}) == std::pair {0.0, 200.0});
    check(moved({1000, 200, 600, 400}) == std::pair {840.0, 200.0});

    // Taller than the screen: the top stays on it, where the title bar is.
    check(moved({100, -300, 600, 1000}) == std::pair {100.0, -125.0});
};

auto tShrinkToFit = test("AU/shrinkToFitKeepsTheRatioAndDeclinesBelowTheFloor") = []
{
    auto shrunk = shrinkToFit({0, 0, 900, 600}, 150);
    check(shrunk.has_value());
    check(shrunk->width == 675 && shrunk->height == 450);

    check(!shrinkToFit({0, 0, 900, 600}, 0).has_value());
    check(!shrinkToFit({0, 0, 900, 600}, -20).has_value());
    check(!shrinkToFit({0, 0, 900, 600}, 400).has_value());

    auto atFloor = shrinkToFit({0, 0, 300, 300}, 60);
    check(atFloor.has_value() && atFloor->height == 240);
    check(!shrinkToFit({0, 0, 300, 300}, 61).has_value());
    check(!shrinkToFit({0, 0, 300, 300}, 10, 295).has_value());
};

auto tOpensConstrained = test("AU/theViewOpensAtTheConstrainedInitialSize") = []
{
    @autoreleasepool
    {
        auto probe = ScopedProbe {};
        probe.record.size = {1000, 100};
        probe.record.limits = resizableBetween({200, 200}, {800, 800});

        auto host = UnitHost<GainPlugin>("Gain");
        auto* view = openView(host.unit);
        check(view != nil);
        check(hasSize(view, 800, 200));
        check(view.autoresizingMask == (NSViewWidthSizable | NSViewHeightSizable));
        check(hasSize(contentOf(probe.record), 800, 200));

        check(probe.record.attached == 1);
        check(probe.record.hostAtAttach != nullptr);
        check(probe.record.editor->host() == probe.record.hostAtAttach);
        check(!probe.record.hostAtAttach->drawsResizeHandle());
        check(probe.record.editor->canRequestResize());
    }
};

auto tFixedSize = test("AU/aFixedSizeEditorPinsTheViewAndCannotAsk") = []
{
    @autoreleasepool
    {
        auto probe = ScopedProbe {};
        probe.record.size = {500, 320};

        auto host = UnitHost<GainPlugin>("Gain");
        auto* view = openView(host.unit);
        check(hasSize(view, 500, 320));
        check(view.autoresizingMask == NSViewNotSizable);

        check(!probe.record.editor->canRequestResize());
        check(!probe.record.editor->requestResize({600, 400}));
        check(hasSize(view, 500, 320));
    }
};

auto tResizeContainer =
    test("AU/aResizeRequestResizesTheViewAndASameSizeContainer") = []
{
    @autoreleasepool
    {
        auto probe = ScopedProbe {};
        probe.record.size = {400, 300};
        probe.record.limits = resizableBetween({200, 200}, {800, 800});

        auto host = UnitHost<GainPlugin>("Gain");
        auto* view = openView(host.unit);
        auto* container = [[NSView alloc] initWithFrame:NSMakeRect(10, 10, 400, 300)];
        [container addSubview:view];

        check(probe.record.editor->requestResize({1000, 500}));
        check(hasSize(view, 800, 500));
        check(hasSize(container, 800, 500));
        check(hasSize(contentOf(probe.record), 800, 500));

        // A superview of another size is the host's layout, not ours to size.
        auto* layout = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 900, 900)];
        [layout addSubview:view];
        check(probe.record.editor->requestResize({300, 300}));
        check(hasSize(view, 300, 300));
        check(hasSize(layout, 900, 900));
    }
};

auto tResizeContentView = test("AU/aResizeRequestLeavesTheWindowsContentViewAlone") = []
{
    @autoreleasepool
    {
        (void) [NSApplication sharedApplication];

        auto probe = ScopedProbe {};
        probe.record.size = {400, 300};
        probe.record.limits = resizableBetween({200, 200}, {800, 800});

        auto host = UnitHost<GainPlugin>("Gain");
        auto* view = openView(host.unit);

        auto* window =
            [[NSWindow alloc] initWithContentRect:NSMakeRect(200, 200, 400, 300)
                                        styleMask:NSWindowStyleMaskBorderless
                                          backing:NSBackingStoreBuffered
                                            defer:YES];
        window.releasedWhenClosed = NO;
        auto* contentView = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 400, 300)];
        window.contentView = contentView;
        [contentView addSubview:view];

        check(probe.record.editor->requestResize({600, 450}));
        check(hasSize(view, 600, 450));
        check(hasSize(contentView, 400, 300));

        [view removeFromSuperview];
    }
};

auto tHostResize = test("AU/aHostResizeReachesTheEditorUnconstrained") = []
{
    @autoreleasepool
    {
        auto probe = ScopedProbe {};
        probe.record.size = {400, 300};
        probe.record.limits = resizableBetween({300, 300}, {500, 500});

        auto host = UnitHost<GainPlugin>("Gain");
        auto* view = openView(host.unit);
        auto* container = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 400, 300)];
        [container addSubview:view];

        [container setFrameSize:NSMakeSize(1200, 150)];
        [view layoutSubtreeIfNeeded];

        check(hasSize(view, 1200, 150));
        check(hasSize(contentOf(probe.record), 1200, 150));
    }
};

auto tUnitClosesFirst = test("AU/closingTheUnitBeforeTheViewClosesTheEditor") = []
{
    auto probe = ScopedProbe {};
    probe.record.limits = resizableBetween({200, 200}, {800, 800});

    @autoreleasepool
    {
        auto host = std::make_unique<UnitHost<GainPlugin>>("Gain");
        auto* view = openView(host->unit);
        check(view != nil);

        auto* attachedHost = probe.record.hostAtAttach;
        host.reset();

        check(probe.record.removed == 1);
        check(probe.record.hostAtRemove == attachedHost);
        check(probe.record.destroyed);

        check([view isKindOfClass:[NSView class]]);
    }

    check(probe.record.removed == 1);
};
} // namespace
