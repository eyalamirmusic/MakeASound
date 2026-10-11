#pragma once

#include "../Core/Editor.h"

#include <optional>

namespace MakeASound::AU
{

// The geometry behind the AU view's window heuristics, on AppKit frames (points,
// y up from the screen's bottom), so it is testable without a window.
struct ViewFrame
{
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
};

struct ViewOrigin
{
    double x = 0.0;
    double y = 0.0;
};

// Chrome a window may add and still read as one that exists to hold the editor.
constexpr auto maxEditorWindowChromeWidth = 80.0;
constexpr auto maxEditorWindowChromeHeight = 200.0;

// The shortest editor the opening correction shrinks to.
constexpr auto minContainedEditorHeight = 240.0;

// Only a window the host opened for the editor is ours to move.
bool isEditorWindow(const ViewFrame& window, const ViewFrame& view) noexcept;

// Whether the view's superview is the host's plugin container, ours to resize
// with the view: never the window's content view, and exactly the view's size.
bool isHostPluginContainer(const ViewFrame& parent,
                           bool parentIsContentView,
                           const ViewFrame& view) noexcept;

// The window's origin moved the least that puts it inside the visible frame;
// one taller than the screen keeps its top on it.
ViewOrigin containedOrigin(const ViewFrame& window,
                           const ViewFrame& visible) noexcept;

// The view shortened by what hangs off the bottom, keeping its ratio; nothing
// when nothing hangs off or the result would be shorter than minimumHeight.
std::optional<EditorSize>
    shrinkToFit(const ViewFrame& view,
                double overhang,
                double minimumHeight = minContainedEditorHeight) noexcept;

} // namespace MakeASound::AU
