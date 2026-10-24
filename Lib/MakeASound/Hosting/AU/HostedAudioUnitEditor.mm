#include "HostedAudioUnitEditor.h"
#include "../UI/NativeViewContainer.h"

#import <AudioUnit/AUCocoaUIView.h>
#import <Cocoa/Cocoa.h>
#import <CoreAudioKit/CoreAudioKit.h>

#include <cstdlib>

namespace MakeASound::Hosting
{

namespace
{
constexpr auto fallbackSize = EditorSize {640, 400};

// A frame smaller than this is a view that has not laid itself out yet.
constexpr CGFloat minimumExtent = 20.0;

bool isUsable(NSSize size)
{
    return size.width >= minimumExtent && size.height >= minimumExtent;
}

// The unit's own view, through the factory class its CocoaUI info names.
NSView* makeCustomView(AudioUnit unit)
{
    auto size = UInt32 {0};
    auto writable = Boolean {0};

    if (AudioUnitGetPropertyInfo(unit,
                                 kAudioUnitProperty_CocoaUI,
                                 kAudioUnitScope_Global,
                                 0,
                                 &size,
                                 &writable)
            != noErr
        || size < sizeof(AudioUnitCocoaViewInfo))
        return nil;

    auto* info = static_cast<AudioUnitCocoaViewInfo*>(std::malloc(size));

    if (info == nullptr)
        return nil;

    NSView* result = nil;

    if (AudioUnitGetProperty(
            unit, kAudioUnitProperty_CocoaUI, kAudioUnitScope_Global, 0, info, &size)
        == noErr)
    {
        auto numClasses = (size - sizeof(CFURLRef)) / sizeof(CFStringRef);
        auto* bundleUrl = (__bridge NSURL*) info->mCocoaAUViewBundleLocation;

        if (numClasses > 0 && bundleUrl != nil)
        {
            auto* className = (__bridge NSString*) info->mCocoaAUViewClass[0];
            auto* bundle = [NSBundle bundleWithURL:bundleUrl];
            Class factoryClass = [bundle classNamed:className];

            auto makesViews = @selector(uiViewForAudioUnit:withSize:);

            if (factoryClass != nil
                && [factoryClass instancesRespondToSelector:makesViews])
            {
                id<AUCocoaUIBase> factory = [[factoryClass alloc] init];
                result = [factory uiViewForAudioUnit:unit withSize:NSMakeSize(0, 0)];
            }
        }

        if (info->mCocoaAUViewBundleLocation != nullptr)
            CFRelease(info->mCocoaAUViewBundleLocation);

        for (auto i = size_t {0}; i < numClasses; ++i)
            if (info->mCocoaAUViewClass[i] != nullptr)
                CFRelease(info->mCocoaAUViewClass[i]);
    }

    std::free(info);
    return result;
}
} // namespace

struct HostedAudioUnitEditor::Impl
{
    void ensureView()
    {
        if (unitView != nil || unit == nullptr)
            return;

        unitView = makeCustomView(unit);
        custom = unitView != nil;

        if (unitView == nil)
            unitView = [[AUGenericView alloc] initWithAudioUnit:unit];

        if (unitView == nil)
            return;

        // A view that resizes itself asks the host to follow.
        [unitView setPostsFrameChangedNotifications:YES];
        frameObserver = [[NSNotificationCenter defaultCenter]
            addObserverForName:NSViewFrameDidChangeNotification
                        object:unitView
                         queue:nil
                    usingBlock:^(NSNotification*) { adoptViewSize(); }];
    }

    void applyContainerSize(int width, int height)
    {
        if (unitView == nil || !attached)
            return;

        applyingHostSize = true;
        [unitView setFrame:NSMakeRect(0, 0, width, height)];
        applyingHostSize = false;
    }

    void adoptViewSize()
    {
        if (unitView == nil || !attached || applyingHostSize)
            return;

        auto frame = [unitView frame];

        if (!isUsable(frame.size) || container == nullptr)
            return;

        auto size = EditorSize {static_cast<int>(frame.size.width),
                                static_cast<int>(frame.size.height)};
        auto bounds = container->getLocalBounds();

        if (static_cast<int>(bounds.w) != size.width
            || static_cast<int>(bounds.h) != size.height)
            editor->requestOwnResize(size);
    }

    AudioUnit unit = nullptr;
    HostedAudioUnitEditor* editor = nullptr;
    NSView* unitView = nil;
    id frameObserver = nil;
    OwningPointer<NativeViewContainer> container;
    bool custom = false;
    bool attached = false;
    bool applyingHostSize = false;
};

HostedAudioUnitEditor::HostedAudioUnitEditor(AudioUnit unitToUse)
    : impl(EA::makeOwned<Impl>())
{
    impl->unit = unitToUse;
    impl->editor = this;
}

HostedAudioUnitEditor::~HostedAudioUnitEditor()
{
    onRemoved();

    if (impl->container)
        impl->container->onResized = {};

    if (impl->frameObserver != nil)
        [[NSNotificationCenter defaultCenter] removeObserver:impl->frameObserver];

    impl->frameObserver = nil;
    impl->unitView = nil;
}

bool HostedAudioUnitEditor::hasCustomView() const
{
    impl->ensureView();
    return impl->custom;
}

EditorSize HostedAudioUnitEditor::initialSize() const
{
    impl->ensureView();

    if (impl->unitView == nil || !isUsable([impl->unitView frame].size))
        return fallbackSize;

    auto frame = [impl->unitView frame];
    return {static_cast<int>(frame.size.width), static_cast<int>(frame.size.height)};
}

EditorConstraints HostedAudioUnitEditor::constraints() const
{
    auto result = EditorConstraints();
    result.resizable = !hasCustomView();
    return result;
}

EditorSize HostedAudioUnitEditor::constrainSize(EditorSize proposed) const
{
    return constraints().resizable ? proposed : initialSize();
}

eacp::Graphics::View& HostedAudioUnitEditor::view()
{
    if (!impl->container)
    {
        impl->container = EA::makeOwned<NativeViewContainer>();
        impl->container->onResized = [owner = impl.get()](int width, int height)
        { owner->applyContainerSize(width, height); };

        auto size = initialSize();
        impl->container->setBounds({0.f,
                                    0.f,
                                    static_cast<float>(size.width),
                                    static_cast<float>(size.height)});
    }

    return *impl->container;
}

void HostedAudioUnitEditor::onAttached()
{
    if (impl->attached)
        return;

    auto& container = static_cast<NativeViewContainer&>(view());
    impl->ensureView();

    auto* parent = (__bridge NSView*) container.getNativeParentHandle();

    if (impl->unitView == nil || parent == nil)
        return;

    [parent addSubview:impl->unitView];
    impl->attached = true;

    auto bounds = container.getLocalBounds();
    auto frame = [impl->unitView frame];
    auto width = static_cast<int>(bounds.w);
    auto height = static_cast<int>(bounds.h);
    auto differs = static_cast<int>(frame.size.width) != width
                   || static_cast<int>(frame.size.height) != height;

    // The view's own size wins when the host can follow it, custom view or not;
    // otherwise the view takes the container's.
    if (differs && isUsable(frame.size)
        && requestOwnResize({static_cast<int>(frame.size.width),
                          static_cast<int>(frame.size.height)}))
        return;

    if (differs)
        impl->applyContainerSize(width, height);
}

void HostedAudioUnitEditor::onRemoved()
{
    if (!impl->attached)
        return;

    impl->attached = false;
    [impl->unitView removeFromSuperview];
}

} // namespace MakeASound::Hosting
