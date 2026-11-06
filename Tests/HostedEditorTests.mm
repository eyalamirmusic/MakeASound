// Hosted editors on macOS: a VST3 fixture's view and Audio Units' views (the
// test module's own, registered here, and Apple's AUDelay) parented into an eacp
// view embedded in a plain NSView, resized through it, asking to be resized, and
// removed; a fixed-size view resizing itself through either format; and no
// editor at all for a host that links no editor factory.

#include "AUTestHost.h"
#include "HostFixtures/InstrumentEditor.h"

#include <MakeASound/Plugin/VST3/VST3Common.h>
#include <MakeASound/Hosting/MakeASoundHosting.h>
#include <MakeASound/Hosting/HostedEditor.h>
#include <MakeASound/Hosting/AU/HostedAudioUnit.h>
#include <MakeASound/Hosting/AU/HostedAudioUnitEditor.h>
#include <MakeASound/Hosting/VST3/HostedVST3Editor.h>

#include <eacp/Graphics/Window/EmbeddedView.h>
#include <eacp/Graphics/Window/NativeChildSurface.h>

#include <eacp/Core/Threads/EventLoop.h>

#include <NanoTest/NanoTest.h>

#import <Cocoa/Cocoa.h>

#include <dlfcn.h>
#include <filesystem>

using namespace nano;
using namespace MakeASound;
using namespace MakeASound::Hosting;

namespace
{
struct DefaultFormats : FormatManager
{
    DefaultFormats() { addDefaultFormats(); }
};

std::string effectFixture()
{
    return (std::filesystem::path(MAKEASOUND_HOST_FIXTURE_VST3_DIR)
            / "HostFixtureEffect.vst3")
        .string();
}

// The host side of an editor: grants every resize and remembers it.
struct RecordingHost : EditorHost
{
    bool resizeEditor(EditorSize size) override
    {
        requests.add(size);
        return true;
    }

    bool drawsResizeHandle() const override { return false; }

    Vector<EditorSize> requests;
};

// The NSView the editor's surface parents the plugin's view into, or nil.
NSView* surfaceOf(Editor& editor)
{
    auto& surface = static_cast<eacp::Graphics::NativeChildSurface&>(editor.view());
    return (__bridge NSView*) surface.getNativeParentHandle();
}

// An editor's view embedded in a plain NSView, as a plugin window would hold it.
struct Embedding
{
    explicit Embedding(Editor& editorToUse)
        : editor(editorToUse)
    {
        auto size = editor.initialSize();
        auto frame = NSMakeRect(0, 0, size.width, size.height);
        parent = [[NSView alloc] initWithFrame:frame];
        embedded = EA::makeOwned<eacp::Graphics::EmbeddedView>(
            (__bridge void*) parent,
            eacp::Graphics::EmbeddedViewOptions {size.width, size.height});
        embedded->setContentView(editor.view());
        editor.setHost(&host);
        editor.onAttached();
    }

    ~Embedding()
    {
        editor.onRemoved();
        editor.setHost(nullptr);
    }

    // What the plugin parented into the surface.
    NSArray<NSView*>* pluginViews()
    {
        auto* native = surfaceOf(editor);
        return native != nil ? native.subviews : @[];
    }

    Editor& editor;
    RecordingHost host;
    NSView* parent = nil;
    OwningPointer<eacp::Graphics::EmbeddedView> embedded;
};

auto tNoFactory = test("HostedEditor/aHostWithoutEditorsShowsNone") = []
{
    const auto formats = DefaultFormats();
    auto description = formats.scanTarget(effectFixture()).scan.classes[0];

    auto plain = formats.load(description);
    check(plain.plugin != nullptr && plain.plugin->createEditor() == nullptr);

    auto shown = formats.load(description, {.editors = hostedEditors()});
    check(shown.plugin != nullptr && shown.plugin->createEditor() != nullptr);
};

auto tVST3 = test("HostedEditor/aVST3ViewAttachesResizesAndDetaches") = []
{
    @autoreleasepool
    {
        const auto formats = DefaultFormats();
        auto description = formats.scanTarget(effectFixture()).scan.classes[0];
        auto loaded = formats.load(description, {.editors = hostedEditors()});
        check(loaded.plugin != nullptr);

        if (loaded.plugin == nullptr)
            return;

        auto editor = loaded.plugin->createEditor();
        auto* hosted = dynamic_cast<HostedVST3Editor*>(editor.get());
        check(hosted != nullptr);

        if (hosted == nullptr)
            return;

        auto size = hosted->initialSize();
        check(size.width > 0 && size.height > 0);

        {
            auto embedding = Embedding(*hosted);
            auto* views = embedding.pluginViews();
            check(views.count == 1);

            // The host resizes: the plugin's view follows the surface.
            if (hosted->constraints().resizable)
            {
                auto wider = EditorSize {size.width + 40, size.height + 30};
                auto target = hosted->constrainSize(wider);
                hosted->view().setBounds({0.f,
                                          0.f,
                                          static_cast<float>(target.width),
                                          static_cast<float>(target.height)});

                if (views.count == 1)
                {
                    auto frame = views[0].frame.size;
                    check(static_cast<int>(frame.width) == target.width);
                    check(static_cast<int>(frame.height) == target.height);
                }
            }
        }

        // Removed with the embedding, the plugin's view is gone from the surface.
        auto* native = surfaceOf(*hosted);
        check(native == nil || native.subviews.count == 0);
    }
};

auto tAudioUnit = test("HostedEditor/anAudioUnitsViewAttachesAndDetaches") = []
{
    @autoreleasepool
    {
        auto component = AUHost::registerComponent(AUHost::descriptionFor("Gain"));
        auto loaded = loadAudioUnit(component, {}, {.editors = hostedEditors()});
        check(loaded.plugin != nullptr);

        if (loaded.plugin == nullptr)
            return;

        auto editor = loaded.plugin->createEditor();
        auto* hosted = dynamic_cast<HostedAudioUnitEditor*>(editor.get());
        check(hosted != nullptr);

        if (hosted == nullptr)
            return;

        auto size = hosted->initialSize();
        check(size.width > 0 && size.height > 0);

        {
            auto embedding = Embedding(*hosted);
            check(embedding.pluginViews().count == 1);
        }

        auto* native = surfaceOf(*hosted);
        check(native == nil || native.subviews.count == 0);
    }
};

auto tAppleView = test("HostedEditor/applesDelayShowsItsOwnView") = []
{
    @autoreleasepool
    {
        const auto formats = DefaultFormats();
        auto scan = formats.scanTarget("aufx:dely:appl").scan;

        if (scan.classes.empty())
            return;

        auto loaded = formats.load(scan.classes[0], {.editors = hostedEditors()});
        check(loaded.plugin != nullptr);

        if (loaded.plugin == nullptr)
            return;

        auto editor = loaded.plugin->createEditor();
        auto* hosted = dynamic_cast<HostedAudioUnitEditor*>(editor.get());
        check(hosted != nullptr);

        if (hosted == nullptr)
            return;

        check(hosted->hasCustomView() == scan.classes[0].hasEditor);
        check(!hosted->constraints().resizable || !hosted->hasCustomView());

        auto embedding = Embedding(*hosted);
        check(embedding.pluginViews().count == 1);
    }
};

// A fixed-size plugin GUI that resizes itself. HostFixtureInstrument's editor
// is not resizable and asks for HostFixtures::resizedSize once shown; a host
// that refused it for not being user-resizable would leave the plugin drawing
// into the wrong size.

bool sameSize(EditorSize a, EditorSize b)
{
    return a.width == b.width && a.height == b.height;
}

// Pumps the loop until the host got the fixture's own resize.
bool receivedOwnResize(RecordingHost& host)
{
    return eacp::Threads::runEventLoopUntil(
        [&]
        {
            for (auto& size: host.requests)
                if (sameSize(size, HostFixtures::resizedSize))
                    return true;

            return false;
        },
        eacp::Time::MS {3000});
}

std::string instrumentFixture()
{
    return (std::filesystem::path(MAKEASOUND_HOST_FIXTURE_VST3_DIR)
            / "HostFixtureInstrument.vst3")
        .string();
}

// The fixture's own component, registered in this process out of its bundle.
AudioComponent registerInstrumentUnit()
{
    using Factory = void* (*)(const AudioComponentDescription*);
    static auto registered = AudioComponent {nullptr};

    if (registered != nullptr)
        return registered;

    auto binary = std::filesystem::path(MAKEASOUND_HOST_FIXTURE_AU_DIR)
                  / "HostFixtureInstrument.component" / "Contents" / "MacOS"
                  / "HostFixtureInstrument";
    auto* module = dlopen(binary.c_str(), RTLD_NOW | RTLD_LOCAL);
    auto* symbol = module != nullptr ? dlsym(module, "MakeASoundAUFactory") : nullptr;

    if (symbol == nullptr)
        return nullptr;

    auto description = AudioComponentDescription {
        .componentType = kAudioUnitType_MusicDevice,
        .componentSubType = FourCC("HfIn").toUint32(),
        .componentManufacturer = FourCC("MkTs").toUint32()};

    registered = AudioComponentRegister(
        &description,
        CFSTR("MakeASound Tests: Host Fixture"),
        0x10000,
        reinterpret_cast<AudioComponentFactoryFunction>(
            reinterpret_cast<Factory>(symbol)));
    return registered;
}

auto tVST3OwnResize = test("HostedEditor/aFixedSizeVST3ViewResizesItself") = []
{
    @autoreleasepool
    {
        const auto formats = DefaultFormats();
        auto description = formats.scanTarget(instrumentFixture()).scan.classes[0];
        auto loaded = formats.load(description, {.editors = hostedEditors()});
        check(loaded.plugin != nullptr);

        if (loaded.plugin == nullptr)
            return;

        auto editor = loaded.plugin->createEditor();
        auto* hosted = dynamic_cast<HostedVST3Editor*>(editor.get());
        check(hosted != nullptr);

        if (hosted == nullptr)
            return;

        check(!hosted->constraints().resizable);
        check(sameSize(hosted->initialSize(), HostFixtures::initialSize));

        auto embedding = Embedding(*hosted);
        check(receivedOwnResize(embedding.host));

        // resizeView returned kResultOk, so the plugin took the size.
        check(sameSize(hosted->initialSize(), HostFixtures::resizedSize));
    }
};

auto tAudioUnitOwnResize =
    test("HostedEditor/aFixedSizeAudioUnitViewResizesItself") = []
{
    @autoreleasepool
    {
        auto component = registerInstrumentUnit();
        check(component != nullptr);

        if (component == nullptr)
            return;

        auto loaded = loadAudioUnit(component, {}, {.editors = hostedEditors()});
        check(loaded.plugin != nullptr);

        if (loaded.plugin == nullptr)
            return;

        auto editor = loaded.plugin->createEditor();
        auto* hosted = dynamic_cast<HostedAudioUnitEditor*>(editor.get());
        check(hosted != nullptr);

        if (hosted == nullptr)
            return;

        check(hosted->hasCustomView());
        check(!hosted->constraints().resizable);
        check(sameSize(hosted->initialSize(), HostFixtures::initialSize));

        auto embedding = Embedding(*hosted);
        check(receivedOwnResize(embedding.host));
        check(sameSize(hosted->initialSize(), HostFixtures::resizedSize));
    }
};
} // namespace
