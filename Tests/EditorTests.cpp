// The editor's size policy: EditorConstraints::constrain, the one rule every
// format applies, and Editor::requestResize's and requestOwnResize's way to its
// host. Plugin core only.

#include <MakeASound/Plugin/MakeASoundPlugin.h>

#include <NanoTest/NanoTest.h>

#include <cstdlib>
#include <stdexcept>
#include <vector>

namespace EditorTests
{
using namespace nano;
using namespace MakeASound;

// Tests live in an anonymous namespace: NanoTest registers a case by
// constructing a namespace-scope variable, so two files naming one the same way
// would otherwise collide at link time.
namespace
{
bool same(EditorSize a, EditorSize b)
{
    return a.width == b.width && a.height == b.height;
}

bool near(EditorSize a, EditorSize b, int tolerance)
{
    return std::abs(a.width - b.width) <= tolerance
           && std::abs(a.height - b.height) <= tolerance;
}

// Within a rounding of each side of the ratio.
bool onRatio(EditorSize size, EditorSize ratio)
{
    auto crossed = size.width * ratio.height - size.height * ratio.width;
    return std::abs(crossed) <= (ratio.width + ratio.height + 1) / 2;
}

EditorConstraints ratioLocked(EditorSize ratio)
{
    return {.resizable = true,
            .minimum = {200, 100},
            .maximum = {1600, 1000},
            .aspectRatio = ratio};
}

struct RecordingHost final : EditorHost
{
    bool resizeEditor(EditorSize size) override
    {
        requests.push_back(size);
        return accepts;
    }

    bool drawsResizeHandle() const override { return false; }

    bool accepts = true;
    std::vector<EditorSize> requests;
};

// No view: these cases never show one.
struct PolicyEditor final : Editor
{
    eacp::Graphics::View& view() override { throw std::logic_error("no view"); }
    EditorConstraints constraints() const override { return limits; }

    EditorConstraints limits;
};

auto tClamp = test("Editor/constrainClampsEachSide") = []
{
    auto limits = EditorConstraints {.minimum = {100, 50}, .maximum = {800, 600}};

    check(same(limits.constrain({10, 10}), {100, 50}));
    check(same(limits.constrain({1000, 1000}), {800, 600}));
    check(same(limits.constrain({10, 1000}), {100, 600}));
    check(same(limits.constrain({300, 200}), {300, 200}));
};

auto tDefaults = test("Editor/defaultConstraintsAreFixedAndUnbounded") = []
{
    auto limits = EditorConstraints {};

    check(!limits.resizable);
    check(same(limits.constrain({640, 480}), {640, 480}));
    check(same(limits.constrain({0, -5}), {1, 1}));
};

auto tSnap = test("Editor/aRatioSnapsAProposalOntoIt") = []
{
    for (auto ratio: {EditorSize {3, 2}, EditorSize {600, 400}, EditorSize {16, 9}})
    {
        auto limits = ratioLocked(ratio);

        for (auto proposal: {EditorSize {700, 400},
                             EditorSize {400, 700},
                             EditorSize {900, 300},
                             EditorSize {1200, 800}})
        {
            auto answer = limits.constrain(proposal);
            check(onRatio(answer, ratio));
        }
    }

    check(same(ratioLocked({3, 2}).constrain({600, 400}), {600, 400}));
    check(same(ratioLocked({600, 400}).constrain({600, 400}), {600, 400}));
};

auto tIdempotent = test("Editor/constrainingAConstrainedSizeBarelyMovesIt") = []
{
    for (auto ratio: {EditorSize {3, 2}, EditorSize {16, 9}, EditorSize {7, 5}})
    {
        auto limits = ratioLocked(ratio);

        for (auto width = 0; width <= 2000; width += 37)
        {
            for (auto height = 0; height <= 1400; height += 41)
            {
                auto once = limits.constrain({width, height});
                auto twice = limits.constrain(once);

                check(near(once, twice, 1));
            }
        }
    }
};

auto tContinuous = test("Editor/nearbyProposalsGiveNearbyAnswers") = []
{
    auto limits = ratioLocked({3, 2});
    auto previous = limits.constrain({0, 500});

    // A drag along each axis and a diagonal, through both bounds.
    for (auto step = 1; step <= 2000; ++step)
    {
        auto answer = limits.constrain({step, 500});
        check(near(answer, previous, 2));
        previous = answer;
    }

    previous = limits.constrain({600, 0});

    for (auto step = 1; step <= 1400; ++step)
    {
        auto answer = limits.constrain({600, step});
        check(near(answer, previous, 2));
        previous = answer;
    }

    previous = limits.constrain({0, 0});

    for (auto step = 1; step <= 1800; ++step)
    {
        auto answer = limits.constrain({step, step});
        check(near(answer, previous, 2));
        previous = answer;
    }
};

auto tZeroSide = test("Editor/aRatioWithAZeroSideLocksNothing") = []
{
    for (auto ratio: {EditorSize {0, 2}, EditorSize {3, 0}, EditorSize {0, 0}})
    {
        auto limits = ratioLocked(ratio);

        check(same(limits.constrain({700, 400}), {700, 400}));
        check(same(limits.constrain({50, 5000}), {200, 1000}));
    }
};

auto tNoSize = test("Editor/aRatioTheBoundsCannotHoldTakesTheMinimum") = []
{
    auto limits = EditorConstraints {.resizable = true,
                                     .minimum = {100, 100},
                                     .maximum = {150, 150},
                                     .aspectRatio = {3, 1}};

    // The smallest size on the ratio that meets the minimum, whatever was asked.
    for (auto proposal:
         {EditorSize {1, 1}, EditorSize {120, 120}, EditorSize {900, 300}})
        check(same(limits.constrain(proposal), {300, 100}));
};

auto tNoHost = test("Editor/requestResizeWithNoHostIsFalse") = []
{
    auto editor = PolicyEditor {};
    editor.limits.resizable = true;

    check(editor.host() == nullptr);
    check(!editor.canRequestResize());
    check(!editor.requestResize({500, 400}));
};

auto tFixed = test("Editor/requestResizeOnAFixedEditorIsFalse") = []
{
    auto host = RecordingHost {};
    auto editor = PolicyEditor {};
    editor.setHost(&host);

    check(editor.host() == &host);
    check(!editor.canRequestResize());
    check(!editor.requestResize({500, 400}));
    check(host.requests.empty());
};

auto tReaches = test("Editor/requestResizeHandsTheHostTheConstrainedSize") = []
{
    auto host = RecordingHost {};
    auto editor = PolicyEditor {};
    editor.limits = {
        .resizable = true, .minimum = {200, 100}, .maximum = {800, 600}};
    editor.setHost(&host);

    check(editor.canRequestResize());
    check(editor.requestResize({1000, 50}));
    check(host.requests.size() == 1);
    check(same(host.requests.back(), {800, 100}));

    host.accepts = false;
    check(!editor.requestResize({300, 300}));
    check(host.requests.size() == 2);
    check(same(host.requests.back(), {300, 300}));

    editor.setHost(nullptr);
    check(!editor.requestResize({300, 300}));
    check(host.requests.size() == 2);
};

auto tOwnFixed = test("Editor/aFixedEditorsOwnResizeReachesTheHost") = []
{
    auto host = RecordingHost {};
    auto editor = PolicyEditor {};
    editor.limits = {.minimum = {200, 100}, .maximum = {800, 600}};

    check(!editor.requestOwnResize({500, 400}));

    editor.setHost(&host);

    // The user may not drag it, but the editor resizes itself.
    check(!editor.canRequestResize());
    check(editor.requestOwnResize({1000, 50}));
    check(host.requests.size() == 1);
    check(same(host.requests.back(), {800, 100}));

    host.accepts = false;
    check(!editor.requestOwnResize({300, 300}));
    check(host.requests.size() == 2);
};

auto tOwnResizable = test("Editor/aResizableEditorsOwnResizeIsARequestResize") = []
{
    auto host = RecordingHost {};
    auto editor = PolicyEditor {};
    editor.limits = ratioLocked({3, 2});
    editor.setHost(&host);

    check(editor.requestOwnResize({900, 500}));
    check(editor.requestResize({900, 500}));
    check(host.requests.size() == 2);
    check(same(host.requests[0], host.requests[1]));
    check(onRatio(host.requests[0], {3, 2}));
};
} // namespace
} // namespace EditorTests
