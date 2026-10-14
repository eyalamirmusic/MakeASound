// The AUv3 view: the extension's principal class as the factory a host asks for a
// unit, and as the view controller showing the plugin's editor, its size policy
// through preferredContentSize, a host's resize, the view configurations a unit
// answers from the editor's constraints, and both closing orders. The same cases
// run over AppKit on macOS and UIKit in the iOS simulator.

#include "AUv3TestHost.h"
#include "AUv3ViewTestHost.h"
#include "ProbeEditor.h"

#include <NanoTest/NanoTest.h>

#import <CoreAudioKit/AUViewController.h>

#include <initializer_list>
#include <memory>

using namespace nano;
using namespace TestPlugins;
using namespace AUv3Host;

namespace
{
using AUv3ViewHost::hostResize;

bool same(CGSize size, double width, double height)
{
    return size.width == width && size.height == height;
}

CGSize contentSize(const EditorRecord& record)
{
    return AUv3ViewHost::sizeOf(record.content->getHandle());
}

AUViewController<AUAudioUnitFactory>* makeController()
{
    return [[AUv3::principalClass() alloc] init];
}

// A controller and the unit it made, the unit held as a host holds it.
struct ViewHost
{
    explicit ViewHost(FourCC code = "Gain", bool loadViewFirst = false)
    {
        registerTestUnits();
        controller = makeController();

        if (loadViewFirst)
            std::ignore = controller.view;

        auto* error = static_cast<NSError*>(nil);
        unit = [controller
            createAudioUnitWithComponentDescription:descriptionFor(code)
                                              error:&error];
        nano::check(unit != nil && error == nil);

        std::ignore = controller.view;
    }

    CGSize viewSize() const { return controller.view.bounds.size; }

    AUViewController<AUAudioUnitFactory>* controller = nil;
    AUAudioUnit* unit = nil;
};

// Runs the main run loop until `done` or a second has passed.
template <typename Predicate>
void pumpUntil(Predicate done)
{
    auto* deadline = [NSDate dateWithTimeIntervalSinceNow:1.0];

    while (!done() && [deadline timeIntervalSinceNow] > 0)
    {
        @autoreleasepool
        {
            [NSRunLoop.currentRunLoop
                runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
        }
    }
}

NSArray<AUAudioUnitViewConfiguration*>*
    configurations(std::initializer_list<CGSize> sizes)
{
    auto* result = [NSMutableArray<AUAudioUnitViewConfiguration*> array];

    for (auto size: sizes)
        [result addObject:[[AUAudioUnitViewConfiguration alloc]
                              initWithWidth:size.width
                                     height:size.height
                          hostHasController:NO]];

    return result;
}

NSIndexSet* indices(std::initializer_list<NSUInteger> values)
{
    auto* result = [NSMutableIndexSet indexSet];

    for (auto value: values)
        [result addIndex:value];

    return result;
}

auto tFactory = test("AUv3/thePrincipalClassCreatesAUnitOfTheRegisteredClass") = []
{
    @autoreleasepool
    {
        registerTestUnits();

        auto* controller = makeController();
        check([controller isKindOfClass:[AUViewController class]]);
        check([controller conformsToProtocol:@protocol(AUAudioUnitFactory)]);

        Module::lastCreated = nullptr;
        auto* error = static_cast<NSError*>(nil);
        auto* unit = [controller
            createAudioUnitWithComponentDescription:descriptionFor("Gain")
                                              error:&error];

        check(unit != nil && error == nil);
        check([unit isMemberOfClass:AUv3::audioUnitClass()]);
        check(dynamic_cast<GainPlugin*>(Module::lastCreated) != nullptr);

        auto unknown = descriptionFor("Gain");
        unknown.componentSubType = FourCC("None").toUint32();
        check([controller createAudioUnitWithComponentDescription:unknown
                                                            error:&error]
              == nil);
        check(error != nil);
    }
};

auto tOpens = test("AUv3/theViewControllerOpensAtTheConstrainedInitialSize") = []
{
    // Outside the pool: the unit is autoreleased, and closes its editor, which
    // writes to the probe's record, only when the pool drains.
    auto probe = ScopedProbe {};

    @autoreleasepool
    {
        probe.record.size = {1000, 100};
        probe.record.limits = resizableBetween({200, 200}, {800, 800});

        auto host = ViewHost {};
        check(same(host.controller.preferredContentSize, 800, 200));
        check(same(host.viewSize(), 800, 200));
        check(same(contentSize(probe.record), 800, 200));

        check(probe.record.attached == 1);
        check(probe.record.hostAtAttach != nullptr);
        check(probe.record.editor->host() == probe.record.hostAtAttach);
        check(!probe.record.hostAtAttach->drawsResizeHandle());
        check(probe.record.editor->canRequestResize());
    }
};

auto tViewFirst = test("AUv3/theEditorOpensWhenTheViewLoadsBeforeTheUnitExists") = []
{
    // Outside the pool: the unit is autoreleased, and closes its editor, which
    // writes to the probe's record, only when the pool drains.
    auto probe = ScopedProbe {};

    @autoreleasepool
    {
        probe.record.size = {500, 320};

        auto host = ViewHost {"Gain", true};
        check(probe.record.attached == 1);
        check(same(host.controller.preferredContentSize, 500, 320));
        check(same(contentSize(probe.record), 500, 320));
    }
};

auto tFixed = test("AUv3/aFixedSizeEditorCannotAskToResize") = []
{
    // Outside the pool: the unit is autoreleased, and closes its editor, which
    // writes to the probe's record, only when the pool drains.
    auto probe = ScopedProbe {};

    @autoreleasepool
    {
        probe.record.size = {500, 320};

        auto host = ViewHost {};
        check(same(host.controller.preferredContentSize, 500, 320));
        check(!probe.record.editor->canRequestResize());
        check(!probe.record.editor->requestResize({600, 400}));
        check(same(host.controller.preferredContentSize, 500, 320));
    }
};

auto tRequest = test("AUv3/aResizeRequestMovesThePreferredContentSize") = []
{
    // Outside the pool: the unit is autoreleased, and closes its editor, which
    // writes to the probe's record, only when the pool drains.
    auto probe = ScopedProbe {};

    @autoreleasepool
    {
        probe.record.size = {400, 300};
        probe.record.limits = resizableBetween({200, 200}, {800, 800});

        auto host = ViewHost {};
        check(probe.record.editor->requestResize({1000, 500}));
        check(same(host.controller.preferredContentSize, 800, 500));

        // The host decides the view's size; the request only states a preference.
        check(same(host.viewSize(), 400, 300));
    }
};

auto tHostResize = test("AUv3/aHostResizeReachesTheEditorUnconstrained") = []
{
    // Outside the pool: the unit is autoreleased, and closes its editor, which
    // writes to the probe's record, only when the pool drains.
    auto probe = ScopedProbe {};

    @autoreleasepool
    {
        probe.record.size = {400, 300};
        probe.record.limits = resizableBetween({300, 300}, {500, 500});

        auto host = ViewHost {};
        hostResize(host.controller, CGSizeMake(1200, 150));

        check(same(host.viewSize(), 1200, 150));
        check(same(contentSize(probe.record), 1200, 150));
        check(same(host.controller.preferredContentSize, 400, 300));
    }
};

auto tConfigurations =
    test("AUv3/viewConfigurationsFollowTheEditorsConstraints") = []
{
    // Outside the pool: the unit is autoreleased, and closes its editor, which
    // writes to the probe's record, only when the pool drains.
    auto probe = ScopedProbe {};

    @autoreleasepool
    {
        registerTestUnits();

        auto* error = static_cast<NSError*>(nil);
        auto* unit = instantiate(descriptionFor("Gain"), &error);
        check(unit != nil);

        // Resizable: the minimum must fit; a zero side matches any, and a size
        // past the maximum still fits, since a host's size is taken as it is.
        probe.record.limits = resizableBetween({300, 200}, {800, 600});
        auto* offered = configurations({{0, 0},
                                        {320, 240},
                                        {200, 500},
                                        {1000, 1000},
                                        {0, 150},
                                        {250, 0},
                                        {0, 200}});
        check([[unit supportedViewConfigurations:offered]
            isEqualToIndexSet:indices({0, 1, 3, 6})]);

        // A ratio's smallest size is the minimum projected onto it.
        probe.record.limits.aspectRatio = {2, 1};
        check([[unit supportedViewConfigurations:configurations({{300, 200},
                                                                 {400, 200},
                                                                 {0, 199}})]
            isEqualToIndexSet:indices({1})]);

        // Fixed: the opening size must fit.
        probe.record.size = {500, 320};
        probe.record.limits = {};
        check([[unit supportedViewConfigurations:configurations({{400, 400},
                                                                 {600, 400},
                                                                 {500, 320},
                                                                 {0, 0},
                                                                 {499, 0}})]
            isEqualToIndexSet:indices({1, 2, 3})]);

        // The throwaway editors were never attached.
        check(probe.record.attached == 0);
    }
};

auto tUnitFirst = test("AUv3/closingTheUnitBeforeTheViewClosesTheEditor") = []
{
    auto probe = ScopedProbe {};
    probe.record.limits = resizableBetween({200, 200}, {800, 800});

    @autoreleasepool
    {
        // Its own pool, so nothing the setup autoreleased keeps the unit alive.
        auto host = std::unique_ptr<ViewHost>();

        @autoreleasepool
        {
            host = std::make_unique<ViewHost>();
        }

        auto* controller = host->controller;
        auto* attachedHost = probe.record.hostAtAttach;
        check(attachedHost != nullptr);

        host->unit = nil;

        check(probe.record.removed == 1);
        check(probe.record.hostAtRemove == attachedHost);
        check(probe.record.destroyed);

        // The view lives on, empty, and a host resize still reaches it.
        hostResize(controller, CGSizeMake(300, 300));
        check(same(controller.view.bounds.size, 300, 300));
    }

    check(probe.record.removed == 1);
};

auto tViewFirstClose = test("AUv3/closingTheViewDetachesTheEditor") = []
{
    auto probe = ScopedProbe {};
    probe.record.limits = resizableBetween({200, 200}, {800, 800});

    @autoreleasepool
    {
        auto* unit = static_cast<AUAudioUnit*>(nil);

        @autoreleasepool
        {
            auto host = ViewHost {};
            unit = host.unit;
            check(probe.record.attached == 1);
        }

        // AppKit keeps a controller whose preferredContentSize changed until its
        // run loop turns, so the view closes once the loop has run.
        pumpUntil([&] { return probe.record.removed > 0; });

        check(probe.record.removed == 1);
        check(probe.record.hostAtRemove != nullptr);
        check(probe.record.destroyed);

        // The unit goes on with no view to close.
        unit = nil;
    }

    check(probe.record.removed == 1);
};
} // namespace
