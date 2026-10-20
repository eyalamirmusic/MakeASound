// The eacp-ui pieces every format shares: the generic editor's size policy and
// the resize grip, with no window.

#include "TestEditor.h"
#include "TestPlugins.h"

#include <MakeASound/Plugin/UI/GenericEditor.h>
#include <MakeASound/Plugin/UI/ResizeGrip.h>

#include <NanoTest/NanoTest.h>

#include <eacp/Graphics/View/View.h>

#include <cmath>
#include <vector>

namespace PluginUITests
{
using namespace nano;
using namespace TestPlugins;

namespace Graphics = eacp::Graphics;

// Tests live in an anonymous namespace: NanoTest registers a case by
// constructing a namespace-scope variable, so two files naming one the same way
// would otherwise collide at link time.
namespace
{
bool same(EditorSize a, EditorSize b)
{
    return a.width == b.width && a.height == b.height;
}

bool within(EditorSize size, const EditorConstraints& limits)
{
    return size.width >= limits.minimum.width && size.width <= limits.maximum.width
           && size.height >= limits.minimum.height
           && size.height <= limits.maximum.height;
}

// Records each request and, when it accepts, sizes the editor's view to it as
// a window would.
struct RecordingHost final : EditorHost
{
    RecordingHost(Editor& editorToUse, bool drawsHandleToUse)
        : editor(editorToUse)
        , drawsHandle(drawsHandleToUse)
    {
    }

    bool resizeEditor(EditorSize size) override
    {
        requests.push_back(size);

        if (accepts)
            editor.view().setBounds({0.f,
                                     0.f,
                                     static_cast<float>(size.width),
                                     static_cast<float>(size.height)});

        return accepts;
    }

    bool drawsResizeHandle() const override { return drawsHandle; }

    Editor& editor;
    bool drawsHandle;
    bool accepts = true;
    std::vector<EditorSize> requests;
};

int countGrips(Editor& editor)
{
    auto count = 0;

    for (auto* child: editor.view().getSubviews())
        if (dynamic_cast<ResizeGrip*>(child) != nullptr)
            ++count;

    return count;
}

int gripsWhenAttachedTo(bool hostDrawsHandle)
{
    auto plugin = GainPlugin {};
    auto editor = GenericEditor(plugin);
    auto host = RecordingHost(editor, hostDrawsHandle);

    editor.setHost(&host);
    editor.onAttached();
    auto grips = countGrips(editor);

    editor.onRemoved();
    check(countGrips(editor) == 0);
    editor.setHost(nullptr);

    return grips;
}

Graphics::MouseEvent mouseAt(Graphics::Point pos, Graphics::Point downPos)
{
    auto event = Graphics::MouseEvent {};
    event.pos = pos;
    event.downPos = downPos;
    return event;
}

auto tPolicy = test("PluginUI/genericEditorIsResizableWithinItsBounds") = []
{
    auto gain = GainPlugin {};
    auto synth = SynthPlugin {};

    for (auto* plugin: {static_cast<Plugin*>(&gain), static_cast<Plugin*>(&synth)})
    {
        auto editor = GenericEditor(*plugin);
        auto limits = editor.constraints();

        check(limits.resizable);
        check(limits.aspectRatio.width == 0 && limits.aspectRatio.height == 0);
        check(limits.minimum.width <= limits.maximum.width);
        check(limits.minimum.height <= limits.maximum.height);

        auto initial = editor.initialSize();
        check(within(initial, limits));
        check(same(limits.constrain(initial), initial));
    }
};

auto tGripNoHandle = test("PluginUI/genericEditorAddsAGripForAHostWithoutOne") = []
{ check(gripsWhenAttachedTo(false) == 1); };

auto tGripHandle = test("PluginUI/genericEditorAddsNoGripForAHostWithOne") = []
{ check(gripsWhenAttachedTo(true) == 0); };

auto tGripNoHost = test("PluginUI/genericEditorAddsNoGripWithNoHost") = []
{
    auto plugin = GainPlugin {};
    auto editor = GenericEditor(plugin);

    editor.onAttached();
    check(countGrips(editor) == 0);
    editor.onRemoved();
};

auto tGripFixed = test("PluginUI/aFixedEditorWantsNoGripAndItsGripIsInert") = []
{
    auto editor = TestEditor {};
    auto host = RecordingHost(editor, false);
    editor.setHost(&host);

    auto grip = ResizeGrip(editor);
    grip.setBounds(ResizeGrip::cornerOf({0.f, 0.f, 400.f, 300.f}));

    check(!ResizeGrip::isWantedBy(editor));
    check(grip.hitTest({5.f, 5.f}) == nullptr);

    editor.limits.resizable = true;
    check(ResizeGrip::isWantedBy(editor));
    check(grip.hitTest({5.f, 5.f}) == &grip);

    host.drawsHandle = true;
    check(!ResizeGrip::isWantedBy(editor));
};

auto tCorner = test("PluginUI/theGripSitsInTheBottomRightCorner") = []
{
    auto corner = ResizeGrip::cornerOf({10.f, 20.f, 400.f, 300.f});

    check(corner.x == 410.f - ResizeGrip::size);
    check(corner.y == 320.f - ResizeGrip::size);
    check(corner.w == ResizeGrip::size && corner.h == ResizeGrip::size);
};

void dragFollowsThePointer(bool hostAccepts)
{
    auto editor = TestEditor {};
    editor.limits = {
        .resizable = true, .minimum = {100, 80}, .maximum = {1000, 800}};

    auto host = RecordingHost(editor, false);
    host.accepts = hostAccepts;
    editor.setHost(&host);

    auto& view = editor.view();
    view.setBounds({0.f, 0.f, 400.f, 300.f});

    auto grip = ResizeGrip(editor);
    view.addSubview(grip);

    auto down = Graphics::Point {9.f, 9.f};
    grip.mouseDown(mouseAt(down, down));

    struct Step
    {
        float dx;
        float dy;
    };

    for (auto step: {Step {10.f, 5.f},
                     Step {60.f, 40.f},
                     Step {61.f, 40.f},
                     Step {900.f, 900.f},
                     Step {-4.f, -4.f},
                     Step {-500.f, -500.f},
                     Step {30.f, -20.f}})
    {
        grip.mouseDragged(mouseAt({down.x + step.dx, down.y + step.dy}, down));

        auto expected =
            editor.limits.constrain({400 + static_cast<int>(std::lround(step.dx)),
                                     300 + static_cast<int>(std::lround(step.dy))});

        check(same(host.requests.back(), expected));
    }

    view.removeSubview(grip);
    editor.setHost(nullptr);
}

auto tDrag = test("PluginUI/aGripDragAsksForTheStartSizePlusTheTravel") = []
{ dragFollowsThePointer(true); };

auto tDragRefused = test("PluginUI/aGripDragIgnoresWhatTheHostDid") = []
{ dragFollowsThePointer(false); };
} // namespace
} // namespace PluginUITests
